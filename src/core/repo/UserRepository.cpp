#include "repo/UserRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"

namespace eden {

namespace {

const char* kColumns = "user_id, email, display_name, theme_prefs, created_at";

User readUser(Statement& stmt) {
    User user;
    user.id = stmt.getText(0);
    user.email = stmt.getText(1);
    user.displayName = stmt.getText(2);
    user.themePrefsJson = stmt.getText(3);
    user.createdAt = stmt.getInt(4);
    return user;
}

}  // namespace

Result<User> UserRepository::insert(User user) {
    if (user.displayName.empty()) {
        return makeError(ErrorCode::InvalidArgument, "user display name is required");
    }
    return guarded<User>([&]() {
        auto guard = db_.lock();
        if (user.id.empty()) user.id = newId();
        if (user.createdAt == 0) user.createdAt = nowMillis();
        Statement stmt = db_.prepare(
            "INSERT INTO users(user_id, email, display_name, theme_prefs, created_at) "
            "VALUES(:id, :email, :name, :theme, :created)");
        stmt.bindText(":id", user.id);
        stmt.bindText(":email", user.email);
        stmt.bindText(":name", user.displayName);
        stmt.bindText(":theme", user.themePrefsJson);
        stmt.bindInt(":created", user.createdAt);
        stmt.execute();
        return user;
    });
}

Result<User> UserRepository::get(const Id& id) {
    return guarded<User>([&]() -> User {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM users WHERE user_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) throw std::runtime_error("user not found: " + id);
        return readUser(stmt);
    });
}

Result<User> UserRepository::getByEmail(const std::string& email) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM users WHERE email = :email");
        stmt.bindText(":email", email);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "no user with email " + email);
        return readUser(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<User>> UserRepository::list() {
    return guarded<std::vector<User>>([&]() {
        auto guard = db_.lock();
        std::vector<User> users;
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM users ORDER BY created_at");
        while (stmt.step()) users.push_back(readUser(stmt));
        return users;
    });
}

Status UserRepository::update(const User& user) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE users SET email = :email, display_name = :name, theme_prefs = :theme "
            "WHERE user_id = :id");
        stmt.bindText(":email", user.email);
        stmt.bindText(":name", user.displayName);
        stmt.bindText(":theme", user.themePrefsJson);
        stmt.bindText(":id", user.id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("user not found: " + user.id);
    });
}

Status UserRepository::remove(const Id& id) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM users WHERE user_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Result<User> UserRepository::ensureDefault(const std::string& displayName) {
    auto existing = list();
    if (!existing.ok()) return existing.error();
    if (!existing.value().empty()) return existing.value().front();
    User user;
    user.displayName = displayName;
    return insert(std::move(user));
}

}  // namespace eden
