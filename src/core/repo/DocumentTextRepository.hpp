#pragma once

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "doc/ExtractedDocument.hpp"

namespace eden {

class DocumentTextRepository {
public:
    explicit DocumentTextRepository(Database& db) : db_(db) {}

    Status put(const Id& docId, const ExtractedDocument& doc);
    Result<ExtractedDocument> get(const Id& docId);
    Result<std::string> slice(const Id& docId, std::int64_t start, std::int64_t length);
    Result<std::int64_t> length(const Id& docId);
    Result<std::vector<ChapterMark>> chapters(const Id& docId);
    Status remove(const Id& docId);

private:
    Database& db_;
};

}  // namespace eden
