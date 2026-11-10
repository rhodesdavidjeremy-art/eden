#include "repo/AiSessionRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"

namespace eden {

namespace {

const char* kColumns =
    "session_id, user_id, COALESCE(pack_id, ''), provider, model, function_id, prompt, response, "
    "input_tokens, output_tokens, latency_ms, status, created_at";

AiSession readSession(Statement& stmt) {
    AiSession session;
    session.id = stmt.getText(0);
    session.userId = stmt.getText(1);
    session.packId = stmt.getText(2);
    session.provider = stmt.getText(3);
    session.model = stmt.getText(4);
    session.functionId = stmt.getText(5);
    session.prompt = stmt.getText(6);
    session.response = stmt.getText(7);
    session.inputTokens = stmt.getInt(8);
    session.outputTokens = stmt.getInt(9);
    session.latencyMs = stmt.getInt(10);
    session.status = sessionStatusFromString(stmt.getText(11));
    session.createdAt = stmt.getInt(12);
    return session;
}

std::vector<AiSession> collect(Statement& stmt) {
    std::vector<AiSession> out;
    while (stmt.step()) out.push_back(readSession(stmt));
    return out;
}

}  // namespace

Result<AiSession> AiSessionRepository::insert(AiSession session) {
    if (session.userId.empty()) return makeError(ErrorCode::InvalidArgument, "session needs a user");
    if (session.provider.empty()) return makeError(ErrorCode::InvalidArgument, "session needs a provider");
    return guarded<AiSession>([&]() {
        auto guard = db_.lock();
        if (session.id.empty()) session.id = newId();
        if (session.createdAt == 0) session.createdAt = nowMillis();
        Statement stmt = db_.prepare(
            "INSERT INTO ai_sessions(session_id, user_id, pack_id, provider, model, function_id, prompt, "
            "response, input_tokens, output_tokens, latency_ms, status, created_at) "
            "VALUES(:id, :user, :pack, :provider, :model, :function, :prompt, :response, :in, :out, "
            ":latency, :status, :created)");
        stmt.bindText(":id", session.id);
        stmt.bindText(":user", session.userId);
        if (session.packId.empty()) {
            stmt.bindNull(":pack");
        } else {
            stmt.bindText(":pack", session.packId);
        }
        stmt.bindText(":provider", session.provider);
        stmt.bindText(":model", session.model);
        stmt.bindText(":function", session.functionId);
        stmt.bindText(":prompt", session.prompt);
        stmt.bindText(":response", session.response);
        stmt.bindInt(":in", session.inputTokens);
        stmt.bindInt(":out", session.outputTokens);
        stmt.bindInt(":latency", session.latencyMs);
        stmt.bindText(":status", toString(session.status));
        stmt.bindInt(":created", session.createdAt);
        stmt.execute();
        return session;
    });
}

Result<AiSession> AiSessionRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM ai_sessions WHERE session_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "session not found: " + id);
        return readSession(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<AiSession>> AiSessionRepository::listByPack(const Id& packId) {
    return guarded<std::vector<AiSession>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM ai_sessions WHERE pack_id = :pack ORDER BY created_at");
        stmt.bindText(":pack", packId);
        return collect(stmt);
    });
}

Result<std::vector<AiSession>> AiSessionRepository::listByPackAndFunction(const Id& packId,
                                                                          const std::string& functionId) {
    return guarded<std::vector<AiSession>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM ai_sessions WHERE pack_id = :pack AND function_id = :function "
                                     "ORDER BY created_at");
        stmt.bindText(":pack", packId);
        stmt.bindText(":function", functionId);
        return collect(stmt);
    });
}

Result<std::vector<AiSession>> AiSessionRepository::recent(const Id& userId, int limit) {
    return guarded<std::vector<AiSession>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM ai_sessions WHERE user_id = :user ORDER BY created_at DESC LIMIT :limit");
        stmt.bindText(":user", userId);
        stmt.bindInt(":limit", limit);
        return collect(stmt);
    });
}

Result<AiSession> AiSessionRepository::latestSuccessful(const Id& packId, const std::string& functionId,
                                                        const std::string& provider) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM ai_sessions WHERE pack_id = :pack AND function_id = :function "
                                     "AND provider = :provider AND status = 'ok' "
                                     "ORDER BY created_at DESC LIMIT 1");
        stmt.bindText(":pack", packId);
        stmt.bindText(":function", functionId);
        stmt.bindText(":provider", provider);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no cached session");
        return readSession(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<ProviderUsage>> AiSessionRepository::usageByProvider(Millis since) {
    return guarded<std::vector<ProviderUsage>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT provider, COUNT(*), SUM(input_tokens), SUM(output_tokens), AVG(latency_ms) "
            "FROM ai_sessions WHERE created_at >= :since GROUP BY provider ORDER BY COUNT(*) DESC");
        stmt.bindInt(":since", since);
        std::vector<ProviderUsage> out;
        while (stmt.step()) {
            ProviderUsage usage;
            usage.provider = stmt.getText(0);
            usage.calls = stmt.getInt(1);
            usage.inputTokens = stmt.getInt(2);
            usage.outputTokens = stmt.getInt(3);
            usage.averageLatencyMs = stmt.getDouble(4);
            out.push_back(std::move(usage));
        }
        return out;
    });
}

Status AiSessionRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM ai_sessions WHERE session_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Result<std::int64_t> AiSessionRepository::purgeOlderThan(Millis cutoff) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM ai_sessions WHERE created_at < :cutoff");
        stmt.bindInt(":cutoff", cutoff);
        stmt.execute();
        return static_cast<std::int64_t>(db_.changes());
    });
}

}  // namespace eden
