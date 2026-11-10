#include "repo/CardRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"
#include "common/Json.hpp"

namespace eden {

namespace {

const char* kColumns =
    "card_id, pack_id, summary_text, evidence_json, ai_images_json, sources_json, revision, "
    "created_at, updated_at";

ResearchCard readCard(Statement& stmt) {
    ResearchCard card;
    card.id = stmt.getText(0);
    card.packId = stmt.getText(1);
    card.summaryText = stmt.getText(2);
    card.evidenceJson = stmt.getText(3);
    card.aiImagesJson = stmt.getText(4);
    card.sourcesJson = stmt.getText(5);
    card.revision = stmt.getInt(6);
    card.createdAt = stmt.getInt(7);
    card.updatedAt = stmt.getInt(8);
    return card;
}

CardSection readSection(Statement& stmt) {
    CardSection section;
    section.cardId = stmt.getText(0);
    section.functionId = stmt.getText(1);
    section.content = stmt.getText(2);
    section.provider = stmt.getText(3);
    section.createdAt = stmt.getInt(4);
    return section;
}

std::string snapshotOf(const ResearchCard& card) {
    Json snapshot = Json::object();
    snapshot["summary"] = card.summaryText;
    snapshot["evidence"] = card.evidenceJson;
    snapshot["images"] = card.aiImagesJson;
    snapshot["sources"] = card.sourcesJson;
    return snapshot.dump();
}

void writeRevision(Database& db, const ResearchCard& card) {
    Statement stmt = db.prepare(
        "INSERT INTO card_revisions(card_id, revision, snapshot_json, created_at) "
        "VALUES(:card, :revision, :snapshot, :now)");
    stmt.bindText(":card", card.id);
    stmt.bindInt(":revision", card.revision);
    stmt.bindText(":snapshot", snapshotOf(card));
    stmt.bindInt(":now", nowMillis());
    stmt.execute();
}

}  // namespace

Result<ResearchCard> CardRepository::create(const Id& packId) {
    if (packId.empty()) return makeError(ErrorCode::InvalidArgument, "card needs a pack");
    return guarded<ResearchCard>([&]() {
        auto guard = db_.lock();
        ResearchCard card;
        card.id = newId();
        card.packId = packId;
        card.createdAt = nowMillis();
        card.updatedAt = card.createdAt;
        Statement stmt = db_.prepare(
            "INSERT INTO research_cards(card_id, pack_id, summary_text, evidence_json, ai_images_json, "
            "sources_json, revision, created_at, updated_at) "
            "VALUES(:id, :pack, '', '[]', '[]', '[]', 1, :created, :updated)");
        stmt.bindText(":id", card.id);
        stmt.bindText(":pack", card.packId);
        stmt.bindInt(":created", card.createdAt);
        stmt.bindInt(":updated", card.updatedAt);
        stmt.execute();
        return card;
    });
}

Result<ResearchCard> CardRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM research_cards WHERE card_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "card not found: " + id);
        return readCard(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<ResearchCard> CardRepository::getByPack(const Id& packId) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM research_cards WHERE pack_id = :pack");
        stmt.bindText(":pack", packId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no card for pack " + packId);
        return readCard(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<ResearchCard> CardRepository::getOrCreateByPack(const Id& packId) {
    auto existing = getByPack(packId);
    if (existing.ok() || existing.error().code != ErrorCode::NotFound) return existing;
    return create(packId);
}

Result<ResearchCard> CardRepository::save(ResearchCard card) {
    return guarded<ResearchCard>([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        card.revision += 1;
        card.updatedAt = nowMillis();
        Statement stmt = db_.prepare(
            "UPDATE research_cards SET summary_text = :summary, evidence_json = :evidence, "
            "ai_images_json = :images, sources_json = :sources, revision = :revision, "
            "updated_at = :updated WHERE card_id = :id");
        stmt.bindText(":summary", card.summaryText);
        stmt.bindText(":evidence", card.evidenceJson);
        stmt.bindText(":images", card.aiImagesJson);
        stmt.bindText(":sources", card.sourcesJson);
        stmt.bindInt(":revision", card.revision);
        stmt.bindInt(":updated", card.updatedAt);
        stmt.bindText(":id", card.id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("card not found: " + card.id);
        writeRevision(db_, card);
        transaction.commit();
        return card;
    });
}

Result<std::vector<ResearchCard>> CardRepository::recent(int limit) {
    return guarded<std::vector<ResearchCard>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM research_cards ORDER BY updated_at DESC LIMIT :limit");
        stmt.bindInt(":limit", limit);
        std::vector<ResearchCard> out;
        while (stmt.step()) out.push_back(readCard(stmt));
        return out;
    });
}

Result<std::vector<ResearchCard>> CardRepository::listByDocument(const Id& docId) {
    return guarded<std::vector<ResearchCard>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT c.card_id, c.pack_id, c.summary_text, c.evidence_json, c.ai_images_json, "
            "c.sources_json, c.revision, c.created_at, c.updated_at FROM research_cards c "
            "JOIN research_packs p ON p.pack_id = c.pack_id WHERE p.doc_id = :doc ORDER BY p.order_index");
        stmt.bindText(":doc", docId);
        std::vector<ResearchCard> out;
        while (stmt.step()) out.push_back(readCard(stmt));
        return out;
    });
}

Result<std::int64_t> CardRepository::countByDocument(const Id& docId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT COUNT(*) FROM research_cards c JOIN research_packs p ON p.pack_id = c.pack_id "
            "WHERE p.doc_id = :doc AND (c.summary_text <> '' OR EXISTS (SELECT 1 FROM card_sections s WHERE s.card_id = c.card_id))");
        stmt.bindText(":doc", docId);
        stmt.step();
        return stmt.getInt(0);
    });
}

Status CardRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM research_cards WHERE card_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status CardRepository::putSection(const CardSection& section) {
    if (section.cardId.empty() || section.functionId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "section needs a card and a function id");
    }
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT INTO card_sections(card_id, function_id, content, provider, created_at) "
            "VALUES(:card, :function, :content, :provider, :now) "
            "ON CONFLICT(card_id, function_id) DO UPDATE SET content = excluded.content, "
            "provider = excluded.provider, created_at = excluded.created_at");
        stmt.bindText(":card", section.cardId);
        stmt.bindText(":function", section.functionId);
        stmt.bindText(":content", section.content);
        stmt.bindText(":provider", section.provider);
        stmt.bindInt(":now", section.createdAt == 0 ? nowMillis() : section.createdAt);
        stmt.execute();
    });
}

Result<CardSection> CardRepository::getSection(const Id& cardId, const std::string& functionId) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT card_id, function_id, content, provider, created_at FROM card_sections "
            "WHERE card_id = :card AND function_id = :function");
        stmt.bindText(":card", cardId);
        stmt.bindText(":function", functionId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "section not found: " + functionId);
        return readSection(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<CardSection>> CardRepository::listSections(const Id& cardId) {
    return guarded<std::vector<CardSection>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT card_id, function_id, content, provider, created_at FROM card_sections "
            "WHERE card_id = :card ORDER BY created_at, function_id");
        stmt.bindText(":card", cardId);
        std::vector<CardSection> out;
        while (stmt.step()) out.push_back(readSection(stmt));
        return out;
    });
}

Status CardRepository::removeSection(const Id& cardId, const std::string& functionId) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM card_sections WHERE card_id = :card AND function_id = :function");
        stmt.bindText(":card", cardId);
        stmt.bindText(":function", functionId);
        stmt.execute();
    });
}

Result<std::vector<CardRevision>> CardRepository::listRevisions(const Id& cardId) {
    return guarded<std::vector<CardRevision>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT revision_id, card_id, revision, snapshot_json, created_at FROM card_revisions "
            "WHERE card_id = :card ORDER BY revision DESC");
        stmt.bindText(":card", cardId);
        std::vector<CardRevision> out;
        while (stmt.step()) {
            CardRevision revision;
            revision.revisionId = stmt.getInt(0);
            revision.cardId = stmt.getText(1);
            revision.revision = stmt.getInt(2);
            revision.snapshotJson = stmt.getText(3);
            revision.createdAt = stmt.getInt(4);
            out.push_back(std::move(revision));
        }
        return out;
    });
}

Result<CardRevision> CardRepository::getRevision(const Id& cardId, std::int64_t revision) {
    auto all = listRevisions(cardId);
    if (!all.ok()) return all.error();
    for (const CardRevision& item : all.value()) {
        if (item.revision == revision) return item;
    }
    return makeError(ErrorCode::NotFound, "revision not found");
}

Result<ResearchCard> CardRepository::restoreRevision(const Id& cardId, std::int64_t revision) {
    auto snapshot = getRevision(cardId, revision);
    if (!snapshot.ok()) return snapshot.error();
    auto parsed = Json::parse(snapshot.value().snapshotJson);
    if (!parsed.ok()) return parsed.error();
    auto current = get(cardId);
    if (!current.ok()) return current.error();
    ResearchCard card = current.value();
    card.summaryText = parsed.value().getString("summary");
    card.evidenceJson = parsed.value().getString("evidence", "[]");
    card.aiImagesJson = parsed.value().getString("images", "[]");
    card.sourcesJson = parsed.value().getString("sources", "[]");
    return save(std::move(card));
}

}  // namespace eden
