#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "doc/PackSegmenter.hpp"
#include "model/Models.hpp"

namespace eden {

struct ImportOptions {
    Id ownerId;
    std::string titleOverride;
    bool autoSegment = true;
    SegmenterOptions segmenter;
    std::string mediaDirectory;
    std::string imageDirectory;
    std::function<void(double, const std::string&)> progress;
    const std::atomic<bool>* cancelFlag = nullptr;
};

struct ImportResult {
    Document document;
    std::int64_t packCount = 0;
    bool alreadyExisted = false;
    std::vector<std::string> warnings;
};

class DocumentImporter {
public:
    explicit DocumentImporter(Database& db) : db_(db) {}

    Result<ImportResult> importFile(const std::string& path, const ImportOptions& options);
    Result<ImportResult> importBytes(const Bytes& data, const std::string& fileName, const ImportOptions& options);
    Result<ResearchPack> createManualPack(const Id& docId, std::int64_t start, std::int64_t end, bool snapToSentences);
    Result<std::int64_t> regenerateAutoPacks(const Id& docId, const SegmenterOptions& options);

private:
    Database& db_;
};

}  // namespace eden
