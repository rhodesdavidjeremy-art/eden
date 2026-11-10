#include "repo/PackRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"
#include "common/Strings.hpp"

namespace eden {

namespace {

const char* kColumns =
    "pack_id, doc_id, start_offset, end_offset, text_content, order_index, page_index, origin, "
    "created_at";

ResearchPack readPack(Statement& stmt, int base = 0) {
    ResearchPack pack;
    pack.id = stmt.getText(base + 0);
    pack.docId = stmt.getText(base + 1);
    pack.startOffset = stmt.getInt(base + 2);
    pack.endOffset = stmt.getInt(base + 3);
    pack.text = stmt.getText(base + 4);
    pack.orderIndex = stmt.getInt(base + 5);
    pack.pageIndex = stmt.getInt(base + 6);
    pack.origin = packOriginFromString(stmt.getText(base + 7));
    pack.createdAt = stmt.getInt(base + 8);
    return pack;
}

void insertRow(Database& db, ResearchPack& pack) {
    if (pack.id.empty()) pack.id = newId();
    if (pack.createdAt == 0) pack.createdAt = nowMillis();
    Statement stmt = db.prepare(
        "INSERT INTO research_packs(pack_id, doc_id, start_offset, end_offset, text_content, "
        "order_index, page_index, origin, created_at) "
        "VALUES(:id, :doc, :start, :end, :text, :order, :page, :origin, :created)");
    stmt.bindText(":id", pack.id);
    stmt.bindText(":doc", pack.docId);
    stmt.bindInt(":start", pack.startOffset);
    stmt.bindInt(":end", pack.endOffset);
    stmt.bindText(":text", pack.text);
    stmt.bindInt(":order", pack.orderIndex);
    stmt.bindInt(":page", pack.pageIndex);
    stmt.bindText(":origin", toString(pack.origin));
    stmt.bindInt(":created", pack.createdAt);
    stmt.execute();
}

std::string buildMatchQuery(const std::string& input) {
    std::string out;
    for (const std::string& token : split(collapseWhitespace(input), ' ', false)) {
        std::string clean;
        for (char c : token) {
            if (c != '"') clean.push_back(c);
        }
        if (clean.empty()) continue;
        if (!out.empty()) out += " ";
        out += "\"" + clean + "\"*";
    }
    return out;
}

Status validate(const ResearchPack& pack) {
    if (pack.docId.empty()) return makeError(ErrorCode::InvalidArgument, "pack needs a document");
    if (pack.text.empty()) return makeError(ErrorCode::InvalidArgument, "pack text is empty");
    if (pack.endOffset < pack.startOffset) {
        return makeError(ErrorCode::InvalidArgument, "pack end offset precedes start offset");
    }
    return Status::success();
}

}  // namespace

Result<ResearchPack> PackRepository::insert(ResearchPack pack) {
    Status check = validate(pack);
    if (!check.ok()) return check.error();
    return guarded<ResearchPack>([&]() {
        auto guard = db_.lock();
        insertRow(db_, pack);
        return pack;
    });
}

Result<std::vector<ResearchPack>> PackRepository::insertMany(std::vector<ResearchPack> packs) {
    for (const ResearchPack& pack : packs) {
        Status check = validate(pack);
        if (!check.ok()) return check.error();
    }
    return guarded<std::vector<ResearchPack>>([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        for (ResearchPack& pack : packs) insertRow(db_, pack);
        transaction.commit();
        return packs;
    });
}

Result<ResearchPack> PackRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM research_packs WHERE pack_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "pack not found: " + id);
        return readPack(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<ResearchPack>> PackRepository::listByDocument(const Id& docId) {
    return guarded<std::vector<ResearchPack>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM research_packs WHERE doc_id = :doc ORDER BY order_index");
        stmt.bindText(":doc", docId);
        std::vector<ResearchPack> out;
        while (stmt.step()) out.push_back(readPack(stmt));
        return out;
    });
}

Result<std::vector<ResearchPack>> PackRepository::listInRange(const Id& docId, std::int64_t startOffset,
                                                              std::int64_t endOffset) {
    return guarded<std::vector<ResearchPack>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM research_packs WHERE doc_id = :doc AND end_offset >= :start "
                                     "AND start_offset <= :end ORDER BY start_offset");
        stmt.bindText(":doc", docId);
        stmt.bindInt(":start", startOffset);
        stmt.bindInt(":end", endOffset);
        std::vector<ResearchPack> out;
        while (stmt.step()) out.push_back(readPack(stmt));
        return out;
    });
}

Result<std::vector<ResearchPack>> PackRepository::listByPage(const Id& docId, std::int64_t pageIndex) {
    return guarded<std::vector<ResearchPack>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM research_packs WHERE doc_id = :doc AND page_index = :page "
                                     "ORDER BY start_offset");
        stmt.bindText(":doc", docId);
        stmt.bindInt(":page", pageIndex);
        std::vector<ResearchPack> out;
        while (stmt.step()) out.push_back(readPack(stmt));
        return out;
    });
}

Result<ResearchPack> PackRepository::findAtOffset(const Id& docId, std::int64_t offset) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM research_packs WHERE doc_id = :doc AND start_offset <= :offset "
                                     "AND end_offset > :offset ORDER BY (end_offset - start_offset) LIMIT 1");
        stmt.bindText(":doc", docId);
        stmt.bindInt(":offset", offset);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no pack covers that offset");
        return readPack(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::int64_t> PackRepository::countByDocument(const Id& docId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT COUNT(*) FROM research_packs WHERE doc_id = :doc");
        stmt.bindText(":doc", docId);
        stmt.step();
        return stmt.getInt(0);
    });
}

Result<std::vector<PackHit>> PackRepository::search(const std::string& query, const Id& docId, int limit) {
    const std::string match = buildMatchQuery(query);
    if (match.empty()) return std::vector<PackHit>{};
    return guarded<std::vector<PackHit>>([&]() {
        auto guard = db_.lock();
        std::string sql =
            "SELECT p.pack_id, p.doc_id, p.start_offset, p.end_offset, p.text_content, p.order_index, "
            "p.page_index, p.origin, p.created_at, "
            "snippet(pack_fts, 0, '[', ']', '...', 16), bm25(pack_fts) "
            "FROM pack_fts JOIN research_packs p ON p.rowid = pack_fts.rowid "
            "WHERE pack_fts MATCH :match";
        if (!docId.empty()) sql += " AND p.doc_id = :doc";
        sql += " ORDER BY bm25(pack_fts) LIMIT :limit";
        Statement stmt = db_.prepare(sql);
        stmt.bindText(":match", match);
        if (!docId.empty()) stmt.bindText(":doc", docId);
        stmt.bindInt(":limit", limit);
        std::vector<PackHit> hits;
        while (stmt.step()) {
            PackHit hit;
            hit.pack = readPack(stmt, 0);
            hit.snippet = stmt.getText(9);
            hit.rank = stmt.getDouble(10);
            hits.push_back(std::move(hit));
        }
        return hits;
    });
}

Status PackRepository::updateText(const Id& id, const std::string& text, std::int64_t startOffset,
                                  std::int64_t endOffset) {
    if (text.empty()) return makeError(ErrorCode::InvalidArgument, "pack text is empty");
    if (endOffset < startOffset) {
        return makeError(ErrorCode::InvalidArgument, "pack end offset precedes start offset");
    }
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE research_packs SET text_content = :text, start_offset = :start, end_offset = :end "
            "WHERE pack_id = :id");
        stmt.bindText(":text", text);
        stmt.bindInt(":start", startOffset);
        stmt.bindInt(":end", endOffset);
        stmt.bindText(":id", id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("pack not found: " + id);
    });
}

Status PackRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM research_packs WHERE pack_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status PackRepository::removeByOrigin(const Id& docId, PackOrigin origin) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM research_packs WHERE doc_id = :doc AND origin = :origin");
        stmt.bindText(":doc", docId);
        stmt.bindText(":origin", toString(origin));
        stmt.execute();
    });
}

Status PackRepository::reindexOrder(const Id& docId) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        std::vector<Id> ordered;
        {
            Statement select = db_.prepare(
                "SELECT pack_id FROM research_packs WHERE doc_id = :doc ORDER BY start_offset, end_offset");
            select.bindText(":doc", docId);
            while (select.step()) ordered.push_back(select.getText(0));
        }
        Statement update = db_.prepare("UPDATE research_packs SET order_index = :order WHERE pack_id = :id");
        std::int64_t index = 0;
        for (const Id& id : ordered) {
            update.reset();
            update.bindInt(":order", index++);
            update.bindText(":id", id);
            update.execute();
        }
        transaction.commit();
    });
}

}  // namespace eden
