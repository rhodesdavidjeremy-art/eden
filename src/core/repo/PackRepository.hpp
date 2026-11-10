#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class PackRepository {
public:
    explicit PackRepository(Database& db) : db_(db) {}

    Result<ResearchPack> insert(ResearchPack pack);
    Result<std::vector<ResearchPack>> insertMany(std::vector<ResearchPack> packs);
    Result<ResearchPack> get(const Id& id);
    Result<std::vector<ResearchPack>> listByDocument(const Id& docId);
    Result<std::vector<ResearchPack>> listInRange(const Id& docId, std::int64_t startOffset,
                                                  std::int64_t endOffset);
    Result<std::vector<ResearchPack>> listByPage(const Id& docId, std::int64_t pageIndex);
    Result<ResearchPack> findAtOffset(const Id& docId, std::int64_t offset);
    Result<std::int64_t> countByDocument(const Id& docId);
    Result<std::vector<PackHit>> search(const std::string& query, const Id& docId, int limit);
    Status updateText(const Id& id, const std::string& text, std::int64_t startOffset,
                      std::int64_t endOffset);
    Status remove(const Id& id);
    Status removeByOrigin(const Id& docId, PackOrigin origin);
    Status reindexOrder(const Id& docId);

private:
    Database& db_;
};

}  // namespace eden
