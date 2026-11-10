#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

std::string extractDomain(const std::string& url);

class SourceRepository {
public:
    explicit SourceRepository(Database& db) : db_(db) {}

    Result<Source> add(Source source);
    Result<Source> get(const Id& id);
    Result<std::vector<Source>> listByCard(const Id& cardId);
    Status setApproved(const Id& id, bool approved);
    Status setTrust(const Id& id, SourceTrust trust);
    Status remove(const Id& id);

    Status addRule(const std::string& domain, RuleKind kind);
    Status removeRule(const std::string& domain);
    Result<std::vector<SourceRule>> listRules();
    bool isAllowed(const std::string& url);

private:
    Database& db_;
};

}  // namespace eden
