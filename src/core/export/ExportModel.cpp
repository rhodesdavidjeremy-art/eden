#include "export/ExportModel.hpp"

#include "common/Clock.hpp"
#include "common/Strings.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CollectionRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

namespace eden {

const char* formatExtension(ExportFormat format) {
    switch (format) {
        case ExportFormat::Markdown: return "md";
        case ExportFormat::Text: return "txt";
        case ExportFormat::Html: return "html";
        case ExportFormat::Docx: return "docx";
        case ExportFormat::Pdf: return "pdf";
        case ExportFormat::EdenPdf: return "pdf";
        case ExportFormat::Pptx: return "pptx";
        case ExportFormat::AnkiTsv: return "tsv";
        case ExportFormat::HtmlSlides: return "html";
    }
    return "txt";
}

const char* formatTitle(ExportFormat format) {
    switch (format) {
        case ExportFormat::Markdown: return "Markdown";
        case ExportFormat::Text: return "Plain text";
        case ExportFormat::Html: return "Web page (HTML)";
        case ExportFormat::Docx: return "Word document";
        case ExportFormat::Pdf: return "PDF";
        case ExportFormat::EdenPdf: return "Eden illuminated PDF";
        case ExportFormat::Pptx: return "PowerPoint presentation";
        case ExportFormat::AnkiTsv: return "Anki flashcards";
        case ExportFormat::HtmlSlides: return "Slide show (HTML)";
    }
    return "";
}

Result<ExportFormat> parseExportFormat(const std::string& name) {
    const std::string key = toLowerAscii(name);
    if (key == "md" || key == "markdown") return ExportFormat::Markdown;
    if (key == "txt" || key == "text") return ExportFormat::Text;
    if (key == "html") return ExportFormat::Html;
    if (key == "docx") return ExportFormat::Docx;
    if (key == "pdf") return ExportFormat::Pdf;
    if (key == "eden-pdf" || key == "edenpdf") return ExportFormat::EdenPdf;
    if (key == "pptx") return ExportFormat::Pptx;
    if (key == "anki" || key == "tsv") return ExportFormat::AnkiTsv;
    if (key == "slides" || key == "html-slides") return ExportFormat::HtmlSlides;
    return makeError(ErrorCode::InvalidArgument, "unknown export format: " + name);
}

std::vector<FlashCard> flashcardsFor(const ExportCard& card) {
    std::vector<FlashCard> out;
    const std::string tag = slugify(card.documentTitle.empty() ? "eden" : card.documentTitle);
    for (const ExportSection& section : card.sections) {
        if (section.data.isNull()) continue;
        for (const Json& item : section.data.items()) {
            if (!item.isObject()) continue;
            if (section.functionId == "output.flashcards") {
                out.push_back({item.getString("front"), item.getString("back"), tag});
            } else if (section.functionId == "glossary") {
                out.push_back({item.getString("term"), item.getString("definition"), tag});
            } else if (section.functionId == "study_questions") {
                out.push_back({item.getString("question"), item.getString("answer_hint"), tag});
            }
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const FlashCard& c) { return trim(c.front).empty() || trim(c.back).empty(); }),
              out.end());
    return out;
}

Result<ExportCard> ExportBuilder::buildCard(const Id& packId, const ExportOptions& options) {
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    DocumentRepository documents(db_);
    auto document = documents.get(pack.value().docId);
    if (!document.ok()) return document.error();

    ExportCard card;
    card.packId = packId;
    card.passage = pack.value().text;
    card.documentTitle = document.value().title;
    card.page = pack.value().pageIndex + 1;
    DocumentTextRepository texts(db_);
    auto chapters = texts.chapters(pack.value().docId);
    if (chapters.ok()) {
        const ChapterMark* current = nullptr;
        for (const ChapterMark& chapter : chapters.value()) {
            if (chapter.offset <= pack.value().startOffset && (current == nullptr || chapter.offset >= current->offset)) current = &chapter;
        }
        if (current != nullptr) card.chapter = current->title;
    }

    CardRepository cards(db_);
    auto stored = cards.getByPack(packId);
    if (!stored.ok()) return card;
    card.summary = stored.value().summaryText;
    auto sections = cards.listSections(stored.value().id);
    if (sections.ok()) {
        std::vector<bool> used(sections.value().size(), false);
        auto consume = [&](const FunctionInfo* info, const std::string& baseId, std::size_t index) {
            const CardSection& item = sections.value()[index];
            ExportSection section;
            section.functionId = baseId;
            section.title = info != nullptr ? info->title : baseId;
            section.group = info != nullptr ? info->group : "other";
            section.provider = item.provider;
            auto envelope = Json::parse(item.content);
            std::string text = item.content;
            if (envelope.ok() && envelope.value().isObject()) {
                text = envelope.value().getString("text");
                section.data = envelope.value().get("data");
            }
            section.text = text;
            section.blocks = renderFunctionResult(baseId, info != nullptr ? info->output : "markdown", section.data, text);
            if (item.functionId != baseId && !item.provider.empty()) section.title += " (" + item.provider + ")";
            card.sections.push_back(std::move(section));
            used[index] = true;
        };
        for (const FunctionInfo& info : directory_.all()) {
            for (std::size_t i = 0; i < sections.value().size(); ++i) {
                const std::string& id = sections.value()[i].functionId;
                if (id == info.id || startsWith(id, info.id + "@")) consume(&info, info.id, i);
            }
        }
        for (std::size_t i = 0; i < sections.value().size(); ++i) {
            if (used[i]) continue;
            const std::string base = sections.value()[i].functionId.substr(0, sections.value()[i].functionId.find('@'));
            consume(directory_.find(base), base, i);
        }
    }
    if (options.includeSources) {
        SourceRepository sources(db_);
        auto listed = sources.listByCard(stored.value().id);
        if (listed.ok()) card.sources = std::move(listed.value());
    }
    if (options.includeComments) {
        auto thread = comments_.thread(stored.value().id);
        if (thread.ok()) {
            for (const CommentView& view : thread.value()) {
                if (view.comment.isPrivate && !view.comment.isWriterOriginal && !options.includePrivateComments) continue;
                if (view.comment.hidden) continue;
                ExportComment comment;
                comment.author = view.authorName;
                comment.body = view.comment.body;
                comment.kind = toString(view.comment.kind);
                comment.mediaName = view.comment.mediaUri;
                comment.writerOriginal = view.comment.isWriterOriginal;
                comment.depth = view.depth;
                card.comments.push_back(std::move(comment));
            }
        }
    }
    if (options.includeImages) {
        auto images = Json::parse(stored.value().aiImagesJson);
        if (images.ok()) {
            for (const Json& item : images.value().items()) {
                ExportImage image;
                image.path = item.isString() ? item.asString() : item.getString("path");
                image.caption = item.isObject() ? item.getString("caption") : std::string();
                if (!image.path.empty()) card.images.push_back(std::move(image));
            }
        }
    }
    return card;
}

Result<ExportBundle> ExportBuilder::forPacks(const std::vector<Id>& packIds, const std::string& title,
                                             const ExportOptions& options) {
    if (packIds.empty()) return makeError(ErrorCode::InvalidArgument, "nothing to export");
    ExportBundle bundle;
    bundle.title = title;
    bundle.generatedAt = formatIso8601(nowMillis());
    std::string documentTitle;
    for (const Id& packId : packIds) {
        auto card = buildCard(packId, options);
        if (!card.ok()) return card.error();
        const bool hasContent = !card.value().summary.empty() || !card.value().sections.empty() || !card.value().comments.empty();
        if (options.onlyCardsWithContent && !hasContent) continue;
        if (documentTitle.empty()) documentTitle = card.value().documentTitle;
        bundle.cards.push_back(std::move(card.value()));
    }
    if (bundle.cards.empty()) return makeError(ErrorCode::NotFound, "none of the selected passages have research content yet");
    bundle.subtitle = documentTitle;
    return bundle;
}

Result<ExportBundle> ExportBuilder::forDocument(const Id& docId, const ExportOptions& options) {
    DocumentRepository documents(db_);
    auto document = documents.get(docId);
    if (!document.ok()) return document.error();
    PackRepository packs(db_);
    auto listed = packs.listByDocument(docId);
    if (!listed.ok()) return listed.error();
    std::vector<Id> ids;
    for (const ResearchPack& pack : listed.value()) ids.push_back(pack.id);
    auto bundle = forPacks(ids, document.value().title + " - Study Guide", options);
    if (!bundle.ok()) return bundle;
    UserRepository users(db_);
    auto owner = users.get(document.value().ownerId);
    if (owner.ok()) bundle.value().author = owner.value().displayName;
    return bundle;
}

Result<ExportBundle> ExportBuilder::forCollection(const Id& collectionId, const ExportOptions& options) {
    CollectionRepository collections(db_);
    auto collection = collections.get(collectionId);
    if (!collection.ok()) return collection.error();
    auto packs = collections.packs(collectionId);
    if (!packs.ok()) return packs.error();
    std::vector<Id> ids;
    for (const ResearchPack& pack : packs.value()) ids.push_back(pack.id);
    auto bundle = forPacks(ids, collection.value().name, options);
    if (!bundle.ok()) return bundle;
    bundle.value().subtitle = collection.value().description;
    return bundle;
}

}  // namespace eden
