#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

enum class DocumentSort { RecentlyOpened, Title, Added };

struct DocumentQuery {
    Id ownerId;
    std::string titleContains;
    DocumentSort sort = DocumentSort::RecentlyOpened;
    int limit = 100;
    int offset = 0;
};

class DocumentRepository {
public:
    explicit DocumentRepository(Database& db) : db_(db) {}

    Result<Document> insert(Document doc);
    Result<Document> get(const Id& id);
    Result<Document> findByHash(const Id& ownerId, const std::string& contentHash);
    Result<std::vector<Document>> list(const DocumentQuery& query);
    Result<std::int64_t> count(const Id& ownerId);
    Status update(const Document& doc);
    Status recordOpened(const Id& id, std::int64_t position);
    Status remove(const Id& id);

private:
    Database& db_;
};

}  // namespace eden
