#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ai/SidecarClient.hpp"
#include "common/Json.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

struct RunOptions {
    std::string provider;
    std::string model;
    Json promptOptions = Json::object();
    bool useCache = true;
    bool refresh = false;
    std::vector<std::string> fallback;
    int timeoutMs = 0;
    const std::atomic<bool>* cancelFlag = nullptr;
};

struct CardResult {
    std::string functionId;
    std::string provider;
    std::string model;
    std::string text;
    Json data;
    bool cached = false;
    std::int64_t inputTokens = 0;
    std::int64_t outputTokens = 0;
    std::int64_t latencyMs = 0;
    std::vector<std::string> warnings;
    Id cardId;
    Id sessionId;

    static CardResult fromJson(const Json& json);
    Json toJson() const;
};

using ChunkHandler = std::function<void(const std::string&)>;

class CardService {
public:
    CardService(Database& db, SidecarClient& sidecar) : db_(db), sidecar_(sidecar) {}

    Result<Json> buildContext(const Id& packId);
    Result<CardResult> run(const Id& userId, const Id& packId, const std::string& functionId,
                           const RunOptions& options = {}, ChunkHandler onChunk = nullptr);
    Result<std::map<std::string, CardResult>> compare(const Id& userId, const Id& packId, const std::string& functionId,
                                                      const std::vector<std::string>& providers,
                                                      const RunOptions& options = {});
    Result<CardResult> chat(const Id& userId, const Id& packId, const std::string& message,
                            const RunOptions& options = {}, ChunkHandler onChunk = nullptr);
    Result<Json> debate(const Id& userId, const Id& packId, const std::string& providerA, const std::string& providerB,
                        int rounds, const std::string& proposition, const RunOptions& options = {});
    Result<Json> search(const std::string& query, const std::string& mode, int limit);
    Result<std::vector<Source>> saveSearchResults(const Id& cardId, const Json& searchResponse,
                                                  const std::vector<std::size_t>& picks);
    Result<Json> formatCitations(const Json& meta, const std::vector<std::string>& styles);
    Result<Json> locateQuote(const Id& packId, const std::string& quote);

    Status configureSidecar(const Json& config);
    Status setProviderKey(const std::string& provider, const std::string& key);
    Result<std::vector<Json>> providers();

private:
    Result<CardResult> persist(const Id& userId, const ResearchPack& pack, const std::string& functionId,
                               const Json& promptOptions, CardResult result);
    Json runParams(const Json& pack, const std::string& functionId, const RunOptions& options, bool stream);
    CallOptions callOptions(const RunOptions& options, const ChunkHandler& onChunk);

    Database& db_;
    SidecarClient& sidecar_;
};

}  // namespace eden
