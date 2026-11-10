#include "app/ReaderModel.hpp"

#include <algorithm>

#include "common/Strings.hpp"
#include "doc/DocumentImporter.hpp"
#include "repo/BookmarkRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"

namespace eden {

namespace {

std::string foldCase(const std::string& text, bool caseSensitive) {
    return caseSensitive ? text : toLowerAscii(text);
}

}  // namespace

ReaderModel::~ReaderModel() { close(); }

Status ReaderModel::open(const Id& docId) {
    close();
    DocumentRepository documents(db_);
    auto document = documents.get(docId);
    if (!document.ok()) return document.error();
    DocumentTextRepository texts(db_);
    auto content = texts.get(docId);
    if (!content.ok()) return content.error();
    document_ = document.value();
    content_ = std::move(content.value());
    if (content_.pages.empty()) {
        return makeError(ErrorCode::InvalidArgument, "document has no pages");
    }
    open_ = true;
    Status loaded = reloadPacks();
    if (!loaded.ok()) {
        open_ = false;
        return loaded;
    }
    const std::int64_t restored = std::min(std::max<std::int64_t>(0, document_.lastPosition), content_.charCount());
    offset_ = restored;
    currentPage_ = content_.pageIndexAt(restored);
    BookmarkRepository bookmarks(db_);
    auto session = bookmarks.startReadingSession(docId, offset_);
    sessionId_ = session.ok() ? session.value() : 0;
    documents.recordOpened(docId, offset_);
    return Status::success();
}

void ReaderModel::close() {
    if (!open_) return;
    persistPosition();
    if (sessionId_ != 0) {
        BookmarkRepository bookmarks(db_);
        bookmarks.endReadingSession(sessionId_, offset_);
        sessionId_ = 0;
    }
    open_ = false;
    packs_.clear();
    packsWithCards_.clear();
    content_ = ExtractedDocument();
}

void ReaderModel::persistPosition() {
    if (!open_) return;
    DocumentRepository documents(db_);
    documents.recordOpened(document_.id, offset_);
}

double ReaderModel::progress() const {
    if (!open_ || content_.charCount() == 0) return 0.0;
    return std::min(1.0, static_cast<double>(offset_) / static_cast<double>(content_.charCount()));
}

Status ReaderModel::loadCardFlags() {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        packsWithCards_.clear();
        Statement stmt = db_.prepare(
            "SELECT p.pack_id FROM research_cards c JOIN research_packs p ON p.pack_id = c.pack_id "
            "WHERE p.doc_id = :doc AND (c.summary_text <> '' OR EXISTS "
            "(SELECT 1 FROM card_sections s WHERE s.card_id = c.card_id) OR EXISTS "
            "(SELECT 1 FROM comments m WHERE m.card_id = c.card_id))");
        stmt.bindText(":doc", document_.id);
        while (stmt.step()) packsWithCards_.insert(stmt.getText(0));
    });
}

Status ReaderModel::reloadPacks() {
    PackRepository packs(db_);
    auto listed = packs.listByDocument(document_.id);
    if (!listed.ok()) return listed.error();
    packs_ = std::move(listed.value());
    std::stable_sort(packs_.begin(), packs_.end(), [](const ResearchPack& a, const ResearchPack& b) {
        if (a.startOffset != b.startOffset) return a.startOffset < b.startOffset;
        return a.endOffset > b.endOffset;
    });
    return loadCardFlags();
}

Result<PageView> ReaderModel::page(std::int64_t index) const {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    if (index < 0 || index >= pageCount()) return makeError(ErrorCode::InvalidArgument, "page out of range");
    const PageSpan& span = content_.pages[static_cast<std::size_t>(index)];
    PageView view;
    view.index = index;
    view.start = span.start;
    view.end = span.end;
    view.pageCount = pageCount();
    view.text = content_.text.substr(static_cast<std::size_t>(span.start), static_cast<std::size_t>(span.end - span.start));
    const ChapterMark* chapter = content_.chapterAt(span.start);
    if (chapter != nullptr) view.chapter = chapter->title;
    view.progress = content_.charCount() > 0 ? static_cast<double>(span.start) / static_cast<double>(content_.charCount()) : 0.0;
    for (const ResearchPack& pack : packs_) {
        if (pack.startOffset >= span.end) break;
        if (pack.endOffset <= span.start) continue;
        PackMark mark;
        mark.packId = pack.id;
        mark.start = std::max(pack.startOffset, span.start) - span.start;
        mark.end = std::min(pack.endOffset, span.end) - span.start;
        mark.continuesFromPrevious = pack.startOffset < span.start;
        mark.continuesOnNext = pack.endOffset > span.end;
        mark.hasCard = packsWithCards_.count(pack.id) > 0;
        mark.origin = pack.origin;
        view.marks.push_back(std::move(mark));
    }
    return view;
}

Status ReaderModel::goToPage(std::int64_t index) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    if (index < 0 || index >= pageCount()) return makeError(ErrorCode::InvalidArgument, "page out of range");
    currentPage_ = index;
    offset_ = content_.pages[static_cast<std::size_t>(index)].start;
    persistPosition();
    return Status::success();
}

Status ReaderModel::next() {
    if (currentPage_ + 1 >= pageCount()) return makeError(ErrorCode::NotFound, "already on the last page");
    return goToPage(currentPage_ + 1);
}

Status ReaderModel::previous() {
    if (currentPage_ <= 0) return makeError(ErrorCode::NotFound, "already on the first page");
    return goToPage(currentPage_ - 1);
}

Status ReaderModel::goToOffset(std::int64_t offset) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    if (offset < 0 || offset > content_.charCount()) return makeError(ErrorCode::InvalidArgument, "offset out of range");
    offset_ = offset;
    currentPage_ = content_.pageIndexAt(offset);
    persistPosition();
    return Status::success();
}

Status ReaderModel::goToChapter(std::size_t index) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    if (index >= content_.chapters.size()) return makeError(ErrorCode::InvalidArgument, "chapter out of range");
    return goToOffset(content_.chapters[index].offset);
}

Result<ResearchPack> ReaderModel::packAt(std::int64_t offset) const {
    const ResearchPack* best = nullptr;
    for (const ResearchPack& pack : packs_) {
        if (pack.startOffset > offset) break;
        if (pack.endOffset <= offset) continue;
        if (best == nullptr || (pack.endOffset - pack.startOffset) < (best->endOffset - best->startOffset)) best = &pack;
    }
    if (best == nullptr) return makeError(ErrorCode::NotFound, "no research pack at that position");
    return *best;
}

Result<ResearchPack> ReaderModel::createPack(std::int64_t start, std::int64_t end, bool snapToSentences) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    DocumentImporter importer(db_);
    auto created = importer.createManualPack(document_.id, start, end, snapToSentences);
    if (!created.ok()) return created;
    Status reloaded = reloadPacks();
    if (!reloaded.ok()) return reloaded.error();
    return created;
}

Status ReaderModel::removePack(const Id& packId) {
    PackRepository packs(db_);
    Status removed = packs.remove(packId);
    if (!removed.ok()) return removed;
    return reloadPacks();
}

Result<Bookmark> ReaderModel::addBookmark(const std::string& label) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    BookmarkRepository bookmarks(db_);
    Bookmark bookmark;
    bookmark.docId = document_.id;
    bookmark.charOffset = offset_;
    bookmark.label = label.empty() ? "Page " + std::to_string(currentPage_ + 1) : label;
    return bookmarks.add(bookmark);
}

Result<std::vector<Bookmark>> ReaderModel::bookmarks() {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    BookmarkRepository bookmarks(db_);
    return bookmarks.listByDocument(document_.id);
}

Status ReaderModel::removeBookmark(const Id& id) {
    BookmarkRepository bookmarks(db_);
    return bookmarks.remove(id);
}

Result<std::vector<PackHit>> ReaderModel::search(const std::string& query, int limit) {
    if (!open_) return makeError(ErrorCode::InvalidArgument, "no document is open");
    PackRepository packs(db_);
    return packs.search(query, document_.id, limit);
}

std::vector<TextSpan> ReaderModel::findInPage(std::int64_t index, const std::string& needle, bool caseSensitive) const {
    std::vector<TextSpan> out;
    if (!open_ || needle.empty() || index < 0 || index >= pageCount()) return out;
    const PageSpan& span = content_.pages[static_cast<std::size_t>(index)];
    const std::string haystack = foldCase(content_.text.substr(static_cast<std::size_t>(span.start),
                                                               static_cast<std::size_t>(span.end - span.start)), caseSensitive);
    const std::string target = foldCase(needle, caseSensitive);
    std::size_t position = 0;
    while ((position = haystack.find(target, position)) != std::string::npos) {
        out.push_back({static_cast<std::int64_t>(position), static_cast<std::int64_t>(position + target.size())});
        position += target.size();
    }
    return out;
}

std::optional<std::int64_t> ReaderModel::findNext(const std::string& needle, std::int64_t fromOffset, bool caseSensitive) const {
    if (!open_ || needle.empty()) return std::nullopt;
    const std::string haystack = foldCase(content_.text, caseSensitive);
    const std::size_t found = haystack.find(foldCase(needle, caseSensitive),
                                            static_cast<std::size_t>(std::max<std::int64_t>(0, fromOffset)));
    if (found == std::string::npos) return std::nullopt;
    return static_cast<std::int64_t>(found);
}

std::optional<std::int64_t> ReaderModel::findPrevious(const std::string& needle, std::int64_t fromOffset, bool caseSensitive) const {
    if (!open_ || needle.empty() || fromOffset <= 0) return std::nullopt;
    const std::string haystack = foldCase(content_.text, caseSensitive);
    const std::size_t found = haystack.rfind(foldCase(needle, caseSensitive), static_cast<std::size_t>(fromOffset - 1));
    if (found == std::string::npos) return std::nullopt;
    return static_cast<std::int64_t>(found);
}

}  // namespace eden
