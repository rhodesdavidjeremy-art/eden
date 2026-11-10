#pragma once

#include <map>
#include <optional>
#include <string>

#include "common/Json.hpp"
#include "common/Result.hpp"
#include "db/Database.hpp"

namespace eden {

class SettingsRepository {
public:
    explicit SettingsRepository(Database& db) : db_(db) {}

    Result<std::optional<std::string>> get(const std::string& key);
    std::string getOr(const std::string& key, const std::string& fallback);
    std::int64_t getIntOr(const std::string& key, std::int64_t fallback);
    bool getBoolOr(const std::string& key, bool fallback);
    Status set(const std::string& key, const std::string& value);
    Status setInt(const std::string& key, std::int64_t value);
    Status setBool(const std::string& key, bool value);
    Status setJson(const std::string& key, const Json& value);
    Json getJsonOr(const std::string& key, const Json& fallback);
    Status remove(const std::string& key);
    Result<std::map<std::string, std::string>> all();
    Result<std::map<std::string, std::string>> withPrefix(const std::string& prefix);

private:
    Database& db_;
};

}  // namespace eden
