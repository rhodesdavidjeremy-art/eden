#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class CollectionRepository {
public:
    explicit CollectionRepository(Database& db) : db_(db) {}

    Result<Collection> create(Collection collection);
    Result<Collection> get(const Id& id);
    Result<std::vector<Collection>> listByOwner(const Id& ownerId);
    Result<std::vector<Collection>> listPublic(int limit);
    Status update(const Collection& collection);
    Status remove(const Id& id);

    Status addPack(const Id& collectionId, const Id& packId);
    Status removePack(const Id& collectionId, const Id& packId);
    Status movePack(const Id& collectionId, const Id& packId, std::int64_t newPosition);
    Result<std::vector<CollectionItem>> items(const Id& collectionId);
    Result<std::vector<ResearchPack>> packs(const Id& collectionId);
    Result<std::int64_t> itemCount(const Id& collectionId);

private:
    void normalizePositions(const Id& collectionId);
    Database& db_;
};

}  // namespace eden
