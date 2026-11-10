#pragma once

#include <string>
#include <vector>

#include "app/FunctionDirectory.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"
#include "doc/DocumentImporter.hpp"

namespace eden {

constexpr int kEdenFormatVersion = 1;

struct EdenPublishOptions {
    std::string authorName;
    std::string description;
    bool includeOriginal = true;
    bool includeImages = true;
    std::vector<Id> packIds;
    std::vector<std::string> suggestedPrompts = {"summary.plain", "keypoints", "assist.critique", "assist.teach"};
};

bool looksLikeEden(const Bytes& data);
Result<Bytes> buildEnhancedDocument(Database& db, const Id& docId, const Id& publisherId, const std::string& mediaDirectory,
                                    const EdenPublishOptions& options = {});
Result<ImportResult> importEnhancedDocument(Database& db, const Bytes& data, const ImportOptions& options);
bool isSafeArchiveName(const std::string& name);

}  // namespace eden
