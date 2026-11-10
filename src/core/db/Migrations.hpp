#pragma once

#include <string>
#include <vector>

#include "db/Database.hpp"

namespace eden {

struct Migration {
    int version;
    std::string name;
    std::string sql;
};

const std::vector<Migration>& allMigrations();
int latestSchemaVersion();
int migrate(Database& db);

}  // namespace eden
