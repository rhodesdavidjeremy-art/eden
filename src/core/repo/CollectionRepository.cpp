#include "repo/CollectionRepository.hpp"

#include <algorithm>

#include "common/Clock.hpp"
#include "common/Ids.hpp"

namespace eden {

namespace {

const char* kColumns = "collection_id, owner_id, name, description, is_public, created_at, updated_at";

Collection readCollection(Statement& stmt) {
    Collection collection;
    collection.id = stmt.getText(0);
    collection.ownerId = stmt.getText(1);
    collection.name = stmt.getText(2);
    collection.description = stmt.getText(3);
    collection.isPublic = stmt.getInt(4) != 0;
    collection.createdAt = stmt.getInt(5);
    collection.updatedAt = stmt.getInt(6);
    return collection;
}

void touch(Database& db, const Id& collectionId) {
    Statement stmt = db.prepare("UPDATE collections SET updated_at = :now WHERE collection_id = :id");
    stmt.bindInt(":now", nowMillis());
    stmt.bindText(":id", collectionId);
    stmt.execute();
}

}  // namespace

Result<Collection> CollectionRepository::create(Collection collection) {
    if (collection.name.empty()) return makeError(ErrorCode::InvalidArgument, "collection name is required");
    if (collection.ownerId.empty()) return makeError(ErrorCode::InvalidArgument, "collection owner is required");
    return guarded<Collection>([&]() {
        auto guard = db_.lock();
        if (collection.id.empty()) collection.id = newId();
        const Millis now = nowMillis();
        if (collection.createdAt == 0) collection.createdAt = now;
        collection.updatedAt = now;
        Statement stmt = db_.prepare(
            "INSERT INTO collections(collection_id, owner_id, name, description, is_public, created_at, updated_at) "
            "VALUES(:id, :owner, :name, :description, :public, :created, :updated)");
        stmt.bindText(":id", collection.id);
        stmt.bindText(":owner", collection.ownerId);
        stmt.bindText(":name", collection.name);
        stmt.bindText(":description", collection.description);
        stmt.bindInt(":public", collection.isPublic ? 1 : 0);
        stmt.bindInt(":created", collection.createdAt);
        stmt.bindInt(":updated", collection.updatedAt);
        stmt.execute();
        return collection;
    });
}

Result<Collection> CollectionRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM collections WHERE collection_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "collection not found: " + id);
        return readCollection(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<Collection>> CollectionRepository::listByOwner(const Id& ownerId) {
    return guarded<std::vector<Collection>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM collections WHERE owner_id = :owner ORDER BY updated_at DESC");
        stmt.bindText(":owner", ownerId);
        std::vector<Collection> out;
        while (stmt.step()) out.push_back(readCollection(stmt));
        return out;
    });
}

Result<std::vector<Collection>> CollectionRepository::listPublic(int limit) {
    return guarded<std::vector<Collection>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM collections WHERE is_public = 1 ORDER BY updated_at DESC LIMIT :limit");
        stmt.bindInt(":limit", limit);
        std::vector<Collection> out;
        while (stmt.step()) out.push_back(readCollection(stmt));
        return out;
    });
}

Status CollectionRepository::update(const Collection& collection) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE collections SET name = :name, description = :description, is_public = :public, "
            "updated_at = :now WHERE collection_id = :id");
        stmt.bindText(":name", collection.name);
        stmt.bindText(":description", collection.description);
        stmt.bindInt(":public", collection.isPublic ? 1 : 0);
        stmt.bindInt(":now", nowMillis());
        stmt.bindText(":id", collection.id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("collection not found: " + collection.id);
    });
}

Status CollectionRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM collections WHERE collection_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status CollectionRepository::addPack(const Id& collectionId, const Id& packId) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        Statement positionStmt = db_.prepare(
            "SELECT COALESCE(MAX(position) + 1, 0) FROM collection_items WHERE collection_id = :collection");
        positionStmt.bindText(":collection", collectionId);
        positionStmt.step();
        const std::int64_t position = positionStmt.getInt(0);
        Statement stmt = db_.prepare(
            "INSERT OR IGNORE INTO collection_items(collection_id, pack_id, position, added_at) "
            "VALUES(:collection, :pack, :position, :now)");
        stmt.bindText(":collection", collectionId);
        stmt.bindText(":pack", packId);
        stmt.bindInt(":position", position);
        stmt.bindInt(":now", nowMillis());
        stmt.execute();
        touch(db_, collectionId);
        transaction.commit();
    });
}

void CollectionRepository::normalizePositions(const Id& collectionId) {
    std::vector<Id> ordered;
    {
        Statement select = db_.prepare(
            "SELECT pack_id FROM collection_items WHERE collection_id = :collection ORDER BY position, added_at");
        select.bindText(":collection", collectionId);
        while (select.step()) ordered.push_back(select.getText(0));
    }
    Statement update = db_.prepare(
        "UPDATE collection_items SET position = :position WHERE collection_id = :collection AND pack_id = :pack");
    std::int64_t index = 0;
    for (const Id& packId : ordered) {
        update.reset();
        update.bindInt(":position", index++);
        update.bindText(":collection", collectionId);
        update.bindText(":pack", packId);
        update.execute();
    }
}

Status CollectionRepository::removePack(const Id& collectionId, const Id& packId) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        Statement stmt = db_.prepare(
            "DELETE FROM collection_items WHERE collection_id = :collection AND pack_id = :pack");
        stmt.bindText(":collection", collectionId);
        stmt.bindText(":pack", packId);
        stmt.execute();
        normalizePositions(collectionId);
        touch(db_, collectionId);
        transaction.commit();
    });
}

Status CollectionRepository::movePack(const Id& collectionId, const Id& packId, std::int64_t newPosition) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Transaction transaction(db_);
        std::vector<Id> ordered;
        {
            Statement select = db_.prepare(
                "SELECT pack_id FROM collection_items WHERE collection_id = :collection ORDER BY position");
            select.bindText(":collection", collectionId);
            while (select.step()) ordered.push_back(select.getText(0));
        }
        auto it = std::find(ordered.begin(), ordered.end(), packId);
        if (it == ordered.end()) throw std::runtime_error("pack is not in the collection");
        ordered.erase(it);
        const std::int64_t clamped =
            std::max<std::int64_t>(0, std::min<std::int64_t>(newPosition, static_cast<std::int64_t>(ordered.size())));
        ordered.insert(ordered.begin() + clamped, packId);
        Statement update = db_.prepare(
            "UPDATE collection_items SET position = :position WHERE collection_id = :collection AND pack_id = :pack");
        std::int64_t index = 0;
        for (const Id& id : ordered) {
            update.reset();
            update.bindInt(":position", index++);
            update.bindText(":collection", collectionId);
            update.bindText(":pack", id);
            update.execute();
        }
        touch(db_, collectionId);
        transaction.commit();
    });
}

Result<std::vector<CollectionItem>> CollectionRepository::items(const Id& collectionId) {
    return guarded<std::vector<CollectionItem>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT collection_id, pack_id, position, added_at FROM collection_items "
            "WHERE collection_id = :collection ORDER BY position");
        stmt.bindText(":collection", collectionId);
        std::vector<CollectionItem> out;
        while (stmt.step()) {
            CollectionItem item;
            item.collectionId = stmt.getText(0);
            item.packId = stmt.getText(1);
            item.position = stmt.getInt(2);
            item.addedAt = stmt.getInt(3);
            out.push_back(std::move(item));
        }
        return out;
    });
}

Result<std::vector<ResearchPack>> CollectionRepository::packs(const Id& collectionId) {
    return guarded<std::vector<ResearchPack>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT p.pack_id, p.doc_id, p.start_offset, p.end_offset, p.text_content, p.order_index, "
            "p.page_index, p.origin, p.created_at FROM collection_items i "
            "JOIN research_packs p ON p.pack_id = i.pack_id WHERE i.collection_id = :collection ORDER BY i.position");
        stmt.bindText(":collection", collectionId);
        std::vector<ResearchPack> out;
        while (stmt.step()) {
            ResearchPack pack;
            pack.id = stmt.getText(0);
            pack.docId = stmt.getText(1);
            pack.startOffset = stmt.getInt(2);
            pack.endOffset = stmt.getInt(3);
            pack.text = stmt.getText(4);
            pack.orderIndex = stmt.getInt(5);
            pack.pageIndex = stmt.getInt(6);
            pack.origin = packOriginFromString(stmt.getText(7));
            pack.createdAt = stmt.getInt(8);
            out.push_back(std::move(pack));
        }
        return out;
    });
}

Result<std::int64_t> CollectionRepository::itemCount(const Id& collectionId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT COUNT(*) FROM collection_items WHERE collection_id = :collection");
        stmt.bindText(":collection", collectionId);
        stmt.step();
        return stmt.getInt(0);
    });
}

}  // namespace eden
