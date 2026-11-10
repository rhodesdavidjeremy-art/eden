#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class CardRepository {
public:
    explicit CardRepository(Database& db) : db_(db) {}

    Result<ResearchCard> create(const Id& packId);
    Result<ResearchCard> get(const Id& id);
    Result<ResearchCard> getByPack(const Id& packId);
    Result<ResearchCard> getOrCreateByPack(const Id& packId);
    Result<ResearchCard> save(ResearchCard card);
    Result<std::vector<ResearchCard>> recent(int limit);
    Result<std::vector<ResearchCard>> listByDocument(const Id& docId);
    Result<std::int64_t> countByDocument(const Id& docId);
    Status remove(const Id& id);

    Status putSection(const CardSection& section);
    Result<CardSection> getSection(const Id& cardId, const std::string& functionId);
    Result<std::vector<CardSection>> listSections(const Id& cardId);
    Status removeSection(const Id& cardId, const std::string& functionId);

    Result<std::vector<CardRevision>> listRevisions(const Id& cardId);
    Result<CardRevision> getRevision(const Id& cardId, std::int64_t revision);
    Result<ResearchCard> restoreRevision(const Id& cardId, std::int64_t revision);

private:
    Database& db_;
};

}  // namespace eden
