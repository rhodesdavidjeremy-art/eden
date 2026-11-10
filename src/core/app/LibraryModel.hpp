#pragma once

#include <string>
#include <vector>

#include "common/Json.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"
#include "doc/DocumentImporter.hpp"
#include "model/Models.hpp"

namespace eden {

enum class LibrarySort { Recent, Title, Added, Progress };

struct LibraryQuery {
    std::string text;
    LibrarySort sort = LibrarySort::Recent;
    int limit = 200;
};

struct LibraryEntry {
    Document document;
    std::int64_t packCount = 0;
    std::int64_t cardCount = 0;
    double progress = 0.0;
    std::string subtitle;
};

class LibraryModel {
public:
    LibraryModel(Database& db, Id ownerId) : db_(db), ownerId_(std::move(ownerId)) {}

    const Id& ownerId() const { return ownerId_; }
    Result<std::vector<LibraryEntry>> entries(const LibraryQuery& query = {});
    Result<ImportResult> import(const std::string& path, ImportOptions options = {});
    Status remove(const Id& docId);
    Status rename(const Id& docId, const std::string& title);
    Result<Json> stats();

private:
    Database& db_;
    Id ownerId_;
};

std::string describeDocument(const Document& document, std::int64_t packCount);

}  // namespace eden
