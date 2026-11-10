#include "repo/SettingsRepository.hpp"

#include "common/Clock.hpp"
#include "common/Strings.hpp"

namespace eden {

Result<std::optional<std::string>> SettingsRepository::get(const std::string& key) {
    return guarded<std::optional<std::string>>([&]() -> std::optional<std::string> {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT value FROM settings WHERE key = :key");
        stmt.bindText(":key", key);
        if (stmt.step()) return stmt.getText(0);
        return std::nullopt;
    });
}

std::string SettingsRepository::getOr(const std::string& key, const std::string& fallback) {
    auto result = get(key);
    if (result.ok() && result.value().has_value()) return *result.value();
    return fallback;
}

std::int64_t SettingsRepository::getIntOr(const std::string& key, std::int64_t fallback) {
    auto result = get(key);
    if (!result.ok() || !result.value().has_value()) return fallback;
    try {
        return std::stoll(*result.value());
    } catch (const std::exception&) {
        return fallback;
    }
}

bool SettingsRepository::getBoolOr(const std::string& key, bool fallback) {
    auto result = get(key);
    if (!result.ok() || !result.value().has_value()) return fallback;
    const std::string text = toLowerAscii(*result.value());
    if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
    if (text == "0" || text == "false" || text == "no" || text == "off") return false;
    return fallback;
}

Status SettingsRepository::set(const std::string& key, const std::string& value) {
    if (key.empty()) return makeError(ErrorCode::InvalidArgument, "setting key is empty");
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT INTO settings(key, value, updated_at) VALUES(:key, :value, :now) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at");
        stmt.bindText(":key", key);
        stmt.bindText(":value", value);
        stmt.bindInt(":now", nowMillis());
        stmt.execute();
    });
}

Status SettingsRepository::setInt(const std::string& key, std::int64_t value) {
    return set(key, std::to_string(value));
}

Status SettingsRepository::setBool(const std::string& key, bool value) {
    return set(key, value ? "true" : "false");
}

Status SettingsRepository::setJson(const std::string& key, const Json& value) {
    return set(key, value.dump());
}

Json SettingsRepository::getJsonOr(const std::string& key, const Json& fallback) {
    auto result = get(key);
    if (!result.ok() || !result.value().has_value()) return fallback;
    auto parsed = Json::parse(*result.value());
    return parsed.ok() ? parsed.value() : fallback;
}

Status SettingsRepository::remove(const std::string& key) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM settings WHERE key = :key");
        stmt.bindText(":key", key);
        stmt.execute();
    });
}

Result<std::map<std::string, std::string>> SettingsRepository::all() {
    return guarded<std::map<std::string, std::string>>([&]() {
        auto guard = db_.lock();
        std::map<std::string, std::string> out;
        Statement stmt = db_.prepare("SELECT key, value FROM settings");
        while (stmt.step()) out[stmt.getText(0)] = stmt.getText(1);
        return out;
    });
}

Result<std::map<std::string, std::string>> SettingsRepository::withPrefix(const std::string& prefix) {
    return guarded<std::map<std::string, std::string>>([&]() {
        auto guard = db_.lock();
        std::map<std::string, std::string> out;
        Statement stmt = db_.prepare("SELECT key, value FROM settings WHERE key LIKE :pattern ESCAPE '\\'");
        stmt.bindText(":pattern", escapeLikePattern(prefix) + "%");
        while (stmt.step()) out[stmt.getText(0)] = stmt.getText(1);
        return out;
    });
}

}  // namespace eden
