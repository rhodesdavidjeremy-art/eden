#include "ai/CardService.hpp"

#include <algorithm>

#include "common/Strings.hpp"
#include "repo/AiSessionRepository.hpp"
#include "repo/CardRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SourceRepository.hpp"

namespace eden {

namespace {

constexpr std::int64_t kContextChars = 600;

std::string safeSlice(DocumentTextRepository& texts, const Id& docId, std::int64_t start, std::int64_t length) {
    if (length <= 0) return std::string();
    auto slice = texts.slice(docId, std::max<std::int64_t>(0, start), length);
    if (!slice.ok()) return std::string();
    return sanitizeUtf8(slice.value());
}

}  // namespace

CardResult CardResult::fromJson(const Json& json) {
    CardResult result;
    result.functionId = json.getString("function_id");
    result.provider = json.getString("provider");
    result.model = json.getString("model");
    result.text = json.getString("text");
    result.data = json.get("data");
    result.cached = json.getBool("cached");
    result.inputTokens = json.get("usage").getInt("input_tokens");
    result.outputTokens = json.get("usage").getInt("output_tokens");
    result.latencyMs = json.getInt("latency_ms");
    result.warnings = json.getStringList("warnings");
    return result;
}

Json CardResult::toJson() const {
    Json out = Json::object();
    out["function_id"] = functionId;
    out["provider"] = provider;
    out["model"] = model;
    out["text"] = text;
    if (!data.isNull()) out["data"] = data;
    out["cached"] = cached;
    out["input_tokens"] = inputTokens;
    out["output_tokens"] = outputTokens;
    out["latency_ms"] = latencyMs;
    out["warnings"] = Json::arrayOfStrings(warnings);
    return out;
}

Result<Json> CardService::buildContext(const Id& packId) {
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    DocumentRepository documents(db_);
    auto document = documents.get(pack.value().docId);
    if (!document.ok()) return document.error();
    DocumentTextRepository texts(db_);
    const ResearchPack& p = pack.value();

    Json context = Json::object();
    context["pack_id"] = p.id;
    context["text"] = p.text;
    context["doc_title"] = document.value().title;
    context["page"] = p.pageIndex + 1;
    context["before"] = safeSlice(texts, p.docId, p.startOffset - kContextChars, std::min(kContextChars, p.startOffset));
    context["after"] = safeSlice(texts, p.docId, p.endOffset, kContextChars);
    auto chapters = texts.chapters(p.docId);
    if (chapters.ok()) {
        const ChapterMark* current = nullptr;
        for (const ChapterMark& chapter : chapters.value()) {
            if (chapter.offset <= p.startOffset && (current == nullptr || chapter.offset >= current->offset)) current = &chapter;
        }
        if (current != nullptr) context["chapter"] = current->title;
    }
    return context;
}

Json CardService::runParams(const Json& pack, const std::string& functionId, const RunOptions& options, bool stream) {
    Json params = Json::object();
    params["function_id"] = functionId;
    params["pack"] = pack;
    if (!options.provider.empty()) params["provider"] = options.provider;
    if (!options.model.empty()) params["model"] = options.model;
    params["options"] = options.promptOptions;
    params["use_cache"] = options.useCache;
    params["refresh"] = options.refresh;
    if (!options.fallback.empty()) params["fallback"] = Json::arrayOfStrings(options.fallback);
    if (stream) params["stream"] = true;
    return params;
}

CallOptions CardService::callOptions(const RunOptions& options, const ChunkHandler& onChunk) {
    CallOptions call;
    call.timeoutMs = options.timeoutMs;
    call.cancelFlag = options.cancelFlag;
    if (onChunk) {
        call.onEvent = [onChunk](const Json& event) {
            if (event.getString("event") == "chunk") onChunk(event.get("data").getString("text"));
        };
    }
    return call;
}

Result<CardResult> CardService::persist(const Id& userId, const ResearchPack& pack, const std::string& functionId,
                                        const Json& promptOptions, CardResult result) {
    try {
        auto guard = db_.lock();
        Transaction transaction(db_);
        CardRepository cards(db_);
        auto card = cards.getOrCreateByPack(pack.id);
        if (!card.ok()) throw std::runtime_error(card.error().message);
        result.cardId = card.value().id;

        Json envelope = Json::object();
        envelope["text"] = result.text;
        if (!result.data.isNull()) envelope["data"] = result.data;
        envelope["model"] = result.model;
        envelope["warnings"] = Json::arrayOfStrings(result.warnings);
        CardSection section;
        section.cardId = card.value().id;
        section.functionId = functionId;
        section.content = envelope.dump();
        section.provider = result.provider;
        Status stored = cards.putSection(section);
        if (!stored.ok()) throw std::runtime_error(stored.error().message);

        if (startsWith(functionId, "summary.") && !result.text.empty() && card.value().summaryText.empty()) {
            ResearchCard updated = card.value();
            updated.summaryText = result.text;
            auto saved = cards.save(updated);
            if (!saved.ok()) throw std::runtime_error(saved.error().message);
        }
        if (!result.cached) {
            AiSessionRepository sessions(db_);
            AiSession session;
            session.userId = userId;
            session.packId = pack.id;
            session.provider = result.provider;
            session.model = result.model;
            session.functionId = functionId;
            session.prompt = "function:" + functionId + " options:" + promptOptions.dump();
            session.response = result.text;
            session.inputTokens = result.inputTokens;
            session.outputTokens = result.outputTokens;
            session.latencyMs = result.latencyMs;
            auto inserted = sessions.insert(session);
            if (!inserted.ok()) throw std::runtime_error(inserted.error().message);
            result.sessionId = inserted.value().id;
        }
        transaction.commit();
        return result;
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
}

Result<CardResult> CardService::run(const Id& userId, const Id& packId, const std::string& functionId,
                                    const RunOptions& options, ChunkHandler onChunk) {
    if (userId.empty()) return makeError(ErrorCode::InvalidArgument, "user id is required");
    auto context = buildContext(packId);
    if (!context.ok()) return context.error();
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    auto reply = sidecar_.call("card.run", runParams(context.value(), functionId, options, static_cast<bool>(onChunk)),
                               callOptions(options, onChunk));
    if (!reply.ok()) return reply.error();
    CardResult result = CardResult::fromJson(reply.value());
    if (result.functionId.empty()) result.functionId = functionId;
    return persist(userId, pack.value(), functionId, options.promptOptions, std::move(result));
}

Result<std::map<std::string, CardResult>> CardService::compare(const Id& userId, const Id& packId,
                                                                const std::string& functionId,
                                                                const std::vector<std::string>& providers,
                                                                const RunOptions& options) {
    auto context = buildContext(packId);
    if (!context.ok()) return context.error();
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    Json params = runParams(context.value(), functionId, options, false);
    params["providers"] = Json::arrayOfStrings(providers);
    auto reply = sidecar_.call("card.compare", params, callOptions(options, nullptr));
    if (!reply.ok()) return reply.error();
    std::map<std::string, CardResult> out;
    for (const auto& [name, value] : reply.value().get("results").members()) {
        CardResult result = CardResult::fromJson(value);
        auto stored = persist(userId, pack.value(), functionId + "@" + name, options.promptOptions, std::move(result));
        if (!stored.ok()) return stored.error();
        out[name] = stored.value();
    }
    return out;
}

Result<CardResult> CardService::chat(const Id& userId, const Id& packId, const std::string& message,
                                     const RunOptions& options, ChunkHandler onChunk) {
    auto context = buildContext(packId);
    if (!context.ok()) return context.error();
    AiSessionRepository sessions(db_);
    Json history = Json::array();
    auto previous = sessions.listByPackAndFunction(packId, "chat");
    if (previous.ok()) {
        const auto& list = previous.value();
        const std::size_t first = list.size() > 12 ? list.size() - 12 : 0;
        for (std::size_t i = first; i < list.size(); ++i) {
            history.push(Json::object({{"role", Json("user")}, {"content", Json(list[i].prompt)}}));
            history.push(Json::object({{"role", Json("assistant")}, {"content", Json(list[i].response)}}));
        }
    }
    Json params = Json::object();
    params["pack"] = context.value();
    params["message"] = message;
    params["history"] = history;
    if (!options.provider.empty()) params["provider"] = options.provider;
    if (!options.model.empty()) params["model"] = options.model;
    params["options"] = options.promptOptions;
    if (onChunk) params["stream"] = true;
    auto reply = sidecar_.call("card.chat", params, callOptions(options, onChunk));
    if (!reply.ok()) return reply.error();
    CardResult result = CardResult::fromJson(reply.value());
    result.functionId = "chat";
    AiSession session;
    session.userId = userId;
    session.packId = packId;
    session.provider = result.provider;
    session.model = result.model;
    session.functionId = "chat";
    session.prompt = message;
    session.response = result.text;
    session.inputTokens = result.inputTokens;
    session.outputTokens = result.outputTokens;
    session.latencyMs = result.latencyMs;
    auto inserted = sessions.insert(session);
    if (!inserted.ok()) return inserted.error();
    result.sessionId = inserted.value().id;
    return result;
}

Result<Json> CardService::debate(const Id& userId, const Id& packId, const std::string& providerA,
                                 const std::string& providerB, int rounds, const std::string& proposition,
                                 const RunOptions& options) {
    auto context = buildContext(packId);
    if (!context.ok()) return context.error();
    Json params = Json::object();
    params["pack"] = context.value();
    params["provider_a"] = providerA;
    params["provider_b"] = providerB;
    params["rounds"] = rounds;
    params["proposition"] = proposition;
    params["options"] = options.promptOptions;
    CallOptions call = callOptions(options, nullptr);
    if (call.timeoutMs <= 0) call.timeoutMs = 300000;
    auto reply = sidecar_.call("card.debate", params, call);
    if (!reply.ok()) return reply.error();
    AiSessionRepository sessions(db_);
    for (const Json& turn : reply.value().get("turns").items()) {
        AiSession session;
        session.userId = userId;
        session.packId = packId;
        session.provider = turn.getString("provider");
        session.model = turn.getString("model");
        session.functionId = "debate";
        session.prompt = "debate " + turn.getString("speaker") + " round " + std::to_string(turn.getInt("round"));
        session.response = turn.getString("text");
        auto inserted = sessions.insert(session);
        if (!inserted.ok()) return inserted.error();
    }
    return reply.value();
}

Result<Json> CardService::search(const std::string& query, const std::string& mode, int limit) {
    SourceRepository sources(db_);
    Json params = Json::object({{"query", Json(query)}, {"mode", Json(mode)}, {"limit", Json(limit)}});
    auto rules = sources.listRules();
    if (rules.ok()) {
        for (const SourceRule& rule : rules.value()) {
            auto set = sidecar_.call("sources.rule", Json::object({{"domain", Json(rule.domain)},
                                                                   {"kind", Json(toString(rule.kind))}}));
            if (!set.ok()) return set.error();
        }
    }
    return sidecar_.call("search.query", params);
}

Result<std::vector<Source>> CardService::saveSearchResults(const Id& cardId, const Json& searchResponse,
                                                           const std::vector<std::size_t>& picks) {
    SourceRepository sources(db_);
    const auto& results = searchResponse.get("results").items();
    std::vector<Source> saved;
    try {
        auto guard = db_.lock();
        Transaction transaction(db_);
        for (std::size_t index : picks) {
            if (index >= results.size()) return makeError(ErrorCode::InvalidArgument, "result index out of range");
            const Json& item = results[index];
            if (!sources.isAllowed(item.getString("url"))) continue;
            Source source;
            source.cardId = cardId;
            source.url = item.getString("url");
            source.title = item.getString("title");
            source.snippet = item.getString("snippet");
            source.publisher = item.getString("domain");
            auto added = sources.add(source);
            if (!added.ok()) throw std::runtime_error(added.error().message);
            saved.push_back(added.value());
        }
        transaction.commit();
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
    return saved;
}

Result<Json> CardService::formatCitations(const Json& meta, const std::vector<std::string>& styles) {
    return sidecar_.call("citation.format", Json::object({{"meta", meta}, {"styles", Json::arrayOfStrings(styles)}}));
}

Result<Json> CardService::locateQuote(const Id& packId, const std::string& quote) {
    PackRepository packs(db_);
    auto pack = packs.get(packId);
    if (!pack.ok()) return pack.error();
    return sidecar_.call("citation.locate",
                         Json::object({{"text", Json(pack.value().text)}, {"quote", Json(quote)}}));
}

Status CardService::configureSidecar(const Json& config) {
    auto reply = sidecar_.call("config.set", Json::object({{"config", config}}));
    if (!reply.ok()) return reply.error();
    return Status::success();
}

Status CardService::setProviderKey(const std::string& provider, const std::string& key) {
    auto reply = sidecar_.call("providers.set_key", Json::object({{"name", Json(provider)}, {"key", Json(key)}}));
    if (!reply.ok()) return reply.error();
    return Status::success();
}

Result<std::vector<Json>> CardService::providers() {
    auto reply = sidecar_.call("providers.list");
    if (!reply.ok()) return reply.error();
    return reply.value().get("providers").items();
}

}  // namespace eden
