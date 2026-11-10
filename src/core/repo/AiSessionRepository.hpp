#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

struct ProviderUsage {
    std::string provider;
    std::int64_t calls = 0;
    std::int64_t inputTokens = 0;
    std::int64_t outputTokens = 0;
    double averageLatencyMs = 0.0;
};

class AiSessionRepository {
public:
    explicit AiSessionRepository(Database& db) : db_(db) {}

    Result<AiSession> insert(AiSession session);
    Result<AiSession> get(const Id& id);
    Result<std::vector<AiSession>> listByPack(const Id& packId);
    Result<std::vector<AiSession>> listByPackAndFunction(const Id& packId, const std::string& functionId);
    Result<std::vector<AiSession>> recent(const Id& userId, int limit);
    Result<AiSession> latestSuccessful(const Id& packId, const std::string& functionId,
                                       const std::string& provider);
    Result<std::vector<ProviderUsage>> usageByProvider(Millis since);
    Status remove(const Id& id);
    Result<std::int64_t> purgeOlderThan(Millis cutoff);

private:
    Database& db_;
};

}  // namespace eden
