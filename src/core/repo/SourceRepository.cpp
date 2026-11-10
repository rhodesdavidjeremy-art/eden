#include "repo/SourceRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"
#include "common/Strings.hpp"

namespace eden {

namespace {

const char* kColumns = "source_id, card_id, url, title, publisher, snippet, trust, approved, created_at";

Source readSource(Statement& stmt) {
    Source source;
    source.id = stmt.getText(0);
    source.cardId = stmt.getText(1);
    source.url = stmt.getText(2);
    source.title = stmt.getText(3);
    source.publisher = stmt.getText(4);
    source.snippet = stmt.getText(5);
    source.trust = sourceTrustFromString(stmt.getText(6));
    source.approved = stmt.getInt(7) != 0;
    source.createdAt = stmt.getInt(8);
    return source;
}

bool domainMatches(const std::string& host, const std::string& rule) {
    if (host == rule) return true;
    return endsWith(host, "." + rule);
}

}  // namespace

std::string extractDomain(const std::string& url) {
    std::string rest = url;
    const std::size_t scheme = rest.find("://");
    if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
    const std::size_t end = rest.find_first_of("/?#");
    if (end != std::string::npos) rest = rest.substr(0, end);
    const std::size_t at = rest.find('@');
    if (at != std::string::npos) rest = rest.substr(at + 1);
    const std::size_t colon = rest.find(':');
    if (colon != std::string::npos) rest = rest.substr(0, colon);
    rest = toLowerAscii(rest);
    if (startsWith(rest, "www.")) rest = rest.substr(4);
    return rest;
}

Result<Source> SourceRepository::add(Source source) {
    if (source.cardId.empty()) return makeError(ErrorCode::InvalidArgument, "source needs a card");
    if (source.url.empty()) return makeError(ErrorCode::InvalidArgument, "source url is empty");
    return guarded<Source>([&]() {
        auto guard = db_.lock();
        if (source.id.empty()) source.id = newId();
        if (source.createdAt == 0) source.createdAt = nowMillis();
        if (source.publisher.empty()) source.publisher = extractDomain(source.url);
        Statement stmt = db_.prepare(
            "INSERT INTO sources(source_id, card_id, url, title, publisher, snippet, trust, approved, created_at) "
            "VALUES(:id, :card, :url, :title, :publisher, :snippet, :trust, :approved, :created)");
        stmt.bindText(":id", source.id);
        stmt.bindText(":card", source.cardId);
        stmt.bindText(":url", source.url);
        stmt.bindText(":title", source.title);
        stmt.bindText(":publisher", source.publisher);
        stmt.bindText(":snippet", source.snippet);
        stmt.bindText(":trust", toString(source.trust));
        stmt.bindInt(":approved", source.approved ? 1 : 0);
        stmt.bindInt(":created", source.createdAt);
        stmt.execute();
        return source;
    });
}

Result<Source> SourceRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM sources WHERE source_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "source not found: " + id);
        return readSource(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<Source>> SourceRepository::listByCard(const Id& cardId) {
    return guarded<std::vector<Source>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM sources WHERE card_id = :card ORDER BY created_at");
        stmt.bindText(":card", cardId);
        std::vector<Source> out;
        while (stmt.step()) out.push_back(readSource(stmt));
        return out;
    });
}

Status SourceRepository::setApproved(const Id& id, bool approved) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("UPDATE sources SET approved = :approved WHERE source_id = :id");
        stmt.bindInt(":approved", approved ? 1 : 0);
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status SourceRepository::setTrust(const Id& id, SourceTrust trust) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("UPDATE sources SET trust = :trust WHERE source_id = :id");
        stmt.bindText(":trust", toString(trust));
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status SourceRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM sources WHERE source_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status SourceRepository::addRule(const std::string& domain, RuleKind kind) {
    const std::string normalized = extractDomain(domain);
    if (normalized.empty()) return makeError(ErrorCode::InvalidArgument, "rule domain is empty");
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT INTO source_rules(domain, kind, created_at) VALUES(:domain, :kind, :now) "
            "ON CONFLICT(domain) DO UPDATE SET kind = excluded.kind");
        stmt.bindText(":domain", normalized);
        stmt.bindText(":kind", toString(kind));
        stmt.bindInt(":now", nowMillis());
        stmt.execute();
    });
}

Status SourceRepository::removeRule(const std::string& domain) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM source_rules WHERE domain = :domain");
        stmt.bindText(":domain", extractDomain(domain));
        stmt.execute();
    });
}

Result<std::vector<SourceRule>> SourceRepository::listRules() {
    return guarded<std::vector<SourceRule>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT rule_id, domain, kind, created_at FROM source_rules ORDER BY domain");
        std::vector<SourceRule> out;
        while (stmt.step()) {
            SourceRule rule;
            rule.id = stmt.getInt(0);
            rule.domain = stmt.getText(1);
            rule.kind = ruleKindFromString(stmt.getText(2));
            rule.createdAt = stmt.getInt(3);
            out.push_back(std::move(rule));
        }
        return out;
    });
}

bool SourceRepository::isAllowed(const std::string& url) {
    const std::string host = extractDomain(url);
    auto rules = listRules();
    if (!rules.ok()) return true;
    bool hasAllowRules = false;
    bool allowMatched = false;
    for (const SourceRule& rule : rules.value()) {
        const bool matches = domainMatches(host, rule.domain);
        if (rule.kind == RuleKind::Block && matches) return false;
        if (rule.kind == RuleKind::Allow) {
            hasAllowRules = true;
            if (matches) allowMatched = true;
        }
    }
    return !hasAllowRules || allowMatched;
}

}  // namespace eden
