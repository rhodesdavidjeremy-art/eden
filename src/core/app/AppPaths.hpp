#pragma once

#include <string>

#include "common/Result.hpp"

namespace eden {

struct AppPaths {
    std::string dataDirectory;
    std::string databasePath;
    std::string cacheDatabasePath;
    std::string imageDirectory;
    std::string keyFilePath;
    std::string logPath;
};

AppPaths pathsUnder(const std::string& dataDirectory);
AppPaths defaultPaths();
Status ensureDirectories(const AppPaths& paths);
Result<std::string> makeTemporaryDirectory(const std::string& prefix);
void removeDirectoryTree(const std::string& path);

}  // namespace eden
