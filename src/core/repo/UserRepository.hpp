#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class UserRepository {
public:
    explicit UserRepository(Database& db) : db_(db) {}

    Result<User> insert(User user);
    Result<User> get(const Id& id);
    Result<User> getByEmail(const std::string& email);
    Result<std::vector<User>> list();
    Status update(const User& user);
    Status remove(const Id& id);
    Result<User> ensureDefault(const std::string& displayName);

private:
    Database& db_;
};

}  // namespace eden
