#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class BookmarkRepository {
public:
    explicit BookmarkRepository(Database& db) : db_(db) {}

    Result<Bookmark> add(Bookmark bookmark);
    Result<std::vector<Bookmark>> listByDocument(const Id& docId);
    Result<Bookmark> nearest(const Id& docId, std::int64_t offset);
    Status remove(const Id& id);
    Result<std::int64_t> startReadingSession(const Id& docId, std::int64_t startOffset);
    Status endReadingSession(std::int64_t sessionId, std::int64_t endOffset);
    Result<std::int64_t> totalReadingMillis(const Id& docId);

private:
    Database& db_;
};

}  // namespace eden
