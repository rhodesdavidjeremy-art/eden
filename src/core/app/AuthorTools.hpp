#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "ai/CardService.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"

namespace eden {

struct AuthorBatchOptions {
    Id docId;
    std::vector<std::string> functionIds;
    std::vector<Id> packIds;
    std::size_t minimumPackCharacters = 40;
    RunOptions run;
    const std::atomic<bool>* cancelFlag = nullptr;
    std::function<void(double, const std::string&)> progress;
};

struct AuthorBatchReport {
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    int cached = 0;
    bool cancelled = false;
    bool stoppedEarly = false;
    std::vector<std::string> errors;
};

Result<AuthorBatchReport> runAuthorBatch(CardService& cards, Database& db, const Id& userId, const AuthorBatchOptions& options);

}  // namespace eden
