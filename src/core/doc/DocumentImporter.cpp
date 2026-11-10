#include "doc/DocumentImporter.hpp"

#include <algorithm>

#include "common/Clock.hpp"
#include "common/Hash.hpp"
#include "common/Ids.hpp"
#include "doc/DocumentLoader.hpp"
#include "doc/EdenFormat.hpp"
#include "doc/Zip.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"

namespace eden {

namespace {

std::vector<ResearchPack> toPacks(const Id& docId, const std::vector<PackDraft>& drafts, PackOrigin origin,
                                  std::int64_t firstOrder) {
    std::vector<ResearchPack> packs;
    packs.reserve(drafts.size());
    std::int64_t order = firstOrder;
    for (const PackDraft& draft : drafts) {
        if (draft.text.empty()) continue;
        ResearchPack pack;
        pack.docId = docId;
        pack.startOffset = draft.start;
        pack.endOffset = draft.end;
        pack.text = draft.text;
        pack.orderIndex = order++;
        pack.pageIndex = draft.pageIndex;
        pack.origin = origin;
        packs.push_back(std::move(pack));
    }
    return packs;
}

}  // namespace

Result<ImportResult> DocumentImporter::importFile(const std::string& path, const ImportOptions& options) {
    auto bytes = readFileBytes(path);
    if (!bytes.ok()) return bytes.error();
    auto result = importBytes(bytes.value(), path, options);
    if (result.ok()) {
        Document& document = result.value().document;
        if (document.filePath != path) {
            document.filePath = path;
            DocumentRepository documents(db_);
            documents.update(document);
        }
    }
    return result;
}

Result<ImportResult> DocumentImporter::importBytes(const Bytes& data, const std::string& fileName,
                                                   const ImportOptions& options) {
    if (options.ownerId.empty()) return makeError(ErrorCode::InvalidArgument, "import needs an owner");
    auto report = [&](double fraction, const char* message) {
        if (options.progress) options.progress(fraction, message);
    };
    auto cancelled = [&]() { return options.cancelFlag != nullptr && options.cancelFlag->load(); };
    report(0.05, "Checking file");
    if (looksLikeEden(data)) return importEnhancedDocument(db_, data, options);
    const std::string hash = sha256Hex(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));

    DocumentRepository documents(db_);
    PackRepository packs(db_);
    auto existing = documents.findByHash(options.ownerId, hash);
    if (existing.ok()) {
        ImportResult result;
        result.document = existing.value();
        result.alreadyExisted = true;
        auto count = packs.countByDocument(result.document.id);
        result.packCount = count.ok() ? count.value() : 0;
        return result;
    }

    if (cancelled()) return makeError(ErrorCode::Cancelled, "import cancelled");
    report(0.15, "Extracting text");
    auto loaded = loadDocumentBytes(data, FileType::Unknown, fileName);
    if (!loaded.ok()) return loaded.error();
    if (cancelled()) return makeError(ErrorCode::Cancelled, "import cancelled");
    report(0.55, "Finding research packs");
    ExtractedDocument extracted = std::move(loaded.value());

    Document document;
    document.title = options.titleOverride.empty() ? extracted.title : options.titleOverride;
    document.fileType = extracted.type;
    document.ownerId = options.ownerId;
    document.filePath = fileName;
    document.contentHash = hash;
    document.pageCount = static_cast<std::int64_t>(extracted.pages.size());
    document.charCount = extracted.charCount();

    ImportResult result;
    result.warnings = extracted.warnings;
    try {
        auto guard = db_.lock();
        Transaction transaction(db_);
        auto inserted = documents.insert(document);
        if (!inserted.ok()) throw std::runtime_error(inserted.error().message);
        document = inserted.value();
        DocumentTextRepository texts(db_);
        Status stored = texts.put(document.id, extracted);
        if (!stored.ok()) throw std::runtime_error(stored.error().message);
        if (options.autoSegment) {
            const auto drafts = segmentDocument(extracted, options.segmenter);
            if (cancelled()) throw std::runtime_error("import cancelled");
            report(0.8, "Saving");
            auto created = packs.insertMany(toPacks(document.id, drafts, PackOrigin::Auto, 0));
            if (!created.ok()) throw std::runtime_error(created.error().message);
            result.packCount = static_cast<std::int64_t>(created.value().size());
        }
        transaction.commit();
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
    result.document = document;
    report(1.0, "Done");
    return result;
}

Result<ResearchPack> DocumentImporter::createManualPack(const Id& docId, std::int64_t start, std::int64_t end,
                                                        bool snapToSentences) {
    DocumentTextRepository texts(db_);
    auto total = texts.length(docId);
    if (!total.ok()) return total.error();
    const std::int64_t margin = 4000;
    const std::int64_t windowStart = std::max<std::int64_t>(0, start - margin);
    const std::int64_t windowEnd = std::min<std::int64_t>(total.value(), end + margin);
    auto window = texts.slice(docId, windowStart, windowEnd - windowStart);
    if (!window.ok()) return window.error();

    ExtractedDocument local;
    local.text = window.value();
    const PackDraft draft = draftFromSelection(local, start - windowStart, end - windowStart, snapToSentences);
    if (draft.text.empty()) return makeError(ErrorCode::InvalidArgument, "selection contains no text");

    PackRepository packs(db_);
    ResearchPack pack;
    pack.docId = docId;
    pack.startOffset = windowStart + draft.start;
    pack.endOffset = windowStart + draft.end;
    pack.text = draft.text;
    pack.origin = PackOrigin::Manual;
    auto existing = packs.listByDocument(docId);
    if (!existing.ok()) return existing.error();
    pack.orderIndex = static_cast<std::int64_t>(existing.value().size());
    auto full = texts.get(docId);
    if (full.ok()) pack.pageIndex = full.value().pageIndexAt(pack.startOffset);
    auto inserted = packs.insert(pack);
    if (!inserted.ok()) return inserted;
    packs.reindexOrder(docId);
    return packs.get(inserted.value().id);
}

Result<std::int64_t> DocumentImporter::regenerateAutoPacks(const Id& docId, const SegmenterOptions& options) {
    DocumentTextRepository texts(db_);
    auto extracted = texts.get(docId);
    if (!extracted.ok()) return extracted.error();
    PackRepository packs(db_);
    try {
        auto guard = db_.lock();
        Transaction transaction(db_);
        Status cleared = packs.removeByOrigin(docId, PackOrigin::Auto);
        if (!cleared.ok()) throw std::runtime_error(cleared.error().message);
        const auto drafts = segmentDocument(extracted.value(), options);
        auto created = packs.insertMany(toPacks(docId, drafts, PackOrigin::Auto, 0));
        if (!created.ok()) throw std::runtime_error(created.error().message);
        Status reindexed = packs.reindexOrder(docId);
        if (!reindexed.ok()) throw std::runtime_error(reindexed.error().message);
        transaction.commit();
        return static_cast<std::int64_t>(created.value().size());
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
}

}  // namespace eden
