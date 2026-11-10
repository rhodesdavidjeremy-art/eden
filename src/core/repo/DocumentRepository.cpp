#include "repo/DocumentRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"
#include "common/Strings.hpp"

namespace eden {

namespace {

const char* kColumns =
    "doc_id, title, file_type, owner_id, is_enhanced, file_path, content_hash, page_count, "
    "char_count, last_opened_at, last_position, created_at";

Document readDocument(Statement& stmt) {
    Document doc;
    doc.id = stmt.getText(0);
    doc.title = stmt.getText(1);
    doc.fileType = fileTypeFromString(stmt.getText(2));
    doc.ownerId = stmt.getText(3);
    doc.isEnhanced = stmt.getInt(4) != 0;
    doc.filePath = stmt.getText(5);
    doc.contentHash = stmt.getText(6);
    doc.pageCount = stmt.getInt(7);
    doc.charCount = stmt.getInt(8);
    doc.lastOpenedAt = stmt.getInt(9);
    doc.lastPosition = stmt.getInt(10);
    doc.createdAt = stmt.getInt(11);
    return doc;
}

}  // namespace

Result<Document> DocumentRepository::insert(Document doc) {
    if (doc.title.empty()) return makeError(ErrorCode::InvalidArgument, "document title is required");
    if (doc.ownerId.empty()) return makeError(ErrorCode::InvalidArgument, "document owner is required");
    if (doc.contentHash.empty()) {
        return makeError(ErrorCode::InvalidArgument, "document content hash is required");
    }
    return guarded<Document>([&]() {
        auto guard = db_.lock();
        if (doc.id.empty()) doc.id = newId();
        if (doc.createdAt == 0) doc.createdAt = nowMillis();
        Statement stmt = db_.prepare(
            "INSERT INTO documents(doc_id, title, file_type, owner_id, is_enhanced, file_path, "
            "content_hash, page_count, char_count, last_opened_at, last_position, created_at) "
            "VALUES(:id, :title, :type, :owner, :enhanced, :path, :hash, :pages, :chars, "
            ":opened, :position, :created)");
        stmt.bindText(":id", doc.id);
        stmt.bindText(":title", doc.title);
        stmt.bindText(":type", toString(doc.fileType));
        stmt.bindText(":owner", doc.ownerId);
        stmt.bindInt(":enhanced", doc.isEnhanced ? 1 : 0);
        stmt.bindText(":path", doc.filePath);
        stmt.bindText(":hash", doc.contentHash);
        stmt.bindInt(":pages", doc.pageCount);
        stmt.bindInt(":chars", doc.charCount);
        stmt.bindInt(":opened", doc.lastOpenedAt);
        stmt.bindInt(":position", doc.lastPosition);
        stmt.bindInt(":created", doc.createdAt);
        stmt.execute();
        return doc;
    });
}

Result<Document> DocumentRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM documents WHERE doc_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "document not found: " + id);
        return readDocument(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<Document> DocumentRepository::findByHash(const Id& ownerId, const std::string& contentHash) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM documents WHERE owner_id = :owner AND content_hash = :hash");
        stmt.bindText(":owner", ownerId);
        stmt.bindText(":hash", contentHash);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no document with that hash");
        return readDocument(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<Document>> DocumentRepository::list(const DocumentQuery& query) {
    return guarded<std::vector<Document>>([&]() {
        auto guard = db_.lock();
        std::string sql = std::string("SELECT ") + kColumns + " FROM documents WHERE 1 = 1";
        if (!query.ownerId.empty()) sql += " AND owner_id = :owner";
        if (!query.titleContains.empty()) sql += " AND title LIKE :title ESCAPE '\\'";
        switch (query.sort) {
            case DocumentSort::RecentlyOpened:
                sql += " ORDER BY MAX(last_opened_at, created_at) DESC";
                break;
            case DocumentSort::Title: sql += " ORDER BY title COLLATE NOCASE ASC"; break;
            case DocumentSort::Added: sql += " ORDER BY created_at DESC"; break;
        }
        sql += " LIMIT :limit OFFSET :offset";
        Statement stmt = db_.prepare(sql);
        if (!query.ownerId.empty()) stmt.bindText(":owner", query.ownerId);
        if (!query.titleContains.empty()) {
            stmt.bindText(":title", "%" + escapeLikePattern(query.titleContains) + "%");
        }
        stmt.bindInt(":limit", query.limit);
        stmt.bindInt(":offset", query.offset);
        std::vector<Document> docs;
        while (stmt.step()) docs.push_back(readDocument(stmt));
        return docs;
    });
}

Result<std::int64_t> DocumentRepository::count(const Id& ownerId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT COUNT(*) FROM documents WHERE (:owner = '' OR owner_id = :owner)");
        stmt.bindText(":owner", ownerId);
        stmt.step();
        return stmt.getInt(0);
    });
}

Status DocumentRepository::update(const Document& doc) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE documents SET title = :title, file_type = :type, is_enhanced = :enhanced, "
            "file_path = :path, content_hash = :hash, page_count = :pages, char_count = :chars, "
            "last_position = :position WHERE doc_id = :id");
        stmt.bindText(":title", doc.title);
        stmt.bindText(":type", toString(doc.fileType));
        stmt.bindInt(":enhanced", doc.isEnhanced ? 1 : 0);
        stmt.bindText(":path", doc.filePath);
        stmt.bindText(":hash", doc.contentHash);
        stmt.bindInt(":pages", doc.pageCount);
        stmt.bindInt(":chars", doc.charCount);
        stmt.bindInt(":position", doc.lastPosition);
        stmt.bindText(":id", doc.id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("document not found: " + doc.id);
    });
}

Status DocumentRepository::recordOpened(const Id& id, std::int64_t position) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE documents SET last_opened_at = :now, last_position = :position WHERE doc_id = :id");
        stmt.bindInt(":now", nowMillis());
        stmt.bindInt(":position", position);
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status DocumentRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM documents WHERE doc_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

}  // namespace eden
