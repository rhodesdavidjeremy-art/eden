#include "repo/BookmarkRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"

namespace eden {

namespace {

Bookmark readBookmark(Statement& stmt) {
    Bookmark bookmark;
    bookmark.id = stmt.getText(0);
    bookmark.docId = stmt.getText(1);
    bookmark.charOffset = stmt.getInt(2);
    bookmark.label = stmt.getText(3);
    bookmark.createdAt = stmt.getInt(4);
    return bookmark;
}

}  // namespace

Result<Bookmark> BookmarkRepository::add(Bookmark bookmark) {
    if (bookmark.docId.empty()) return makeError(ErrorCode::InvalidArgument, "bookmark needs a document");
    return guarded<Bookmark>([&]() {
        auto guard = db_.lock();
        if (bookmark.id.empty()) bookmark.id = newId();
        if (bookmark.createdAt == 0) bookmark.createdAt = nowMillis();
        Statement stmt = db_.prepare(
            "INSERT INTO bookmarks(bookmark_id, doc_id, char_offset, label, created_at) "
            "VALUES(:id, :doc, :offset, :label, :created)");
        stmt.bindText(":id", bookmark.id);
        stmt.bindText(":doc", bookmark.docId);
        stmt.bindInt(":offset", bookmark.charOffset);
        stmt.bindText(":label", bookmark.label);
        stmt.bindInt(":created", bookmark.createdAt);
        stmt.execute();
        return bookmark;
    });
}

Result<std::vector<Bookmark>> BookmarkRepository::listByDocument(const Id& docId) {
    return guarded<std::vector<Bookmark>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT bookmark_id, doc_id, char_offset, label, created_at FROM bookmarks "
            "WHERE doc_id = :doc ORDER BY char_offset");
        stmt.bindText(":doc", docId);
        std::vector<Bookmark> out;
        while (stmt.step()) out.push_back(readBookmark(stmt));
        return out;
    });
}

Result<Bookmark> BookmarkRepository::nearest(const Id& docId, std::int64_t offset) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT bookmark_id, doc_id, char_offset, label, created_at FROM bookmarks "
            "WHERE doc_id = :doc ORDER BY ABS(char_offset - :offset) LIMIT 1");
        stmt.bindText(":doc", docId);
        stmt.bindInt(":offset", offset);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no bookmarks in document");
        return readBookmark(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Status BookmarkRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM bookmarks WHERE bookmark_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Result<std::int64_t> BookmarkRepository::startReadingSession(const Id& docId, std::int64_t startOffset) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT INTO reading_sessions(doc_id, started_at, start_offset, end_offset) "
            "VALUES(:doc, :now, :offset, :offset)");
        stmt.bindText(":doc", docId);
        stmt.bindInt(":now", nowMillis());
        stmt.bindInt(":offset", startOffset);
        stmt.execute();
        return db_.lastInsertRowId();
    });
}

Status BookmarkRepository::endReadingSession(std::int64_t sessionId, std::int64_t endOffset) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE reading_sessions SET ended_at = :now, end_offset = :offset WHERE session_id = :id");
        stmt.bindInt(":now", nowMillis());
        stmt.bindInt(":offset", endOffset);
        stmt.bindInt(":id", sessionId);
        stmt.execute();
    });
}

Result<std::int64_t> BookmarkRepository::totalReadingMillis(const Id& docId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT COALESCE(SUM(ended_at - started_at), 0) FROM reading_sessions "
            "WHERE doc_id = :doc AND ended_at > 0");
        stmt.bindText(":doc", docId);
        stmt.step();
        return stmt.getInt(0);
    });
}

}  // namespace eden
