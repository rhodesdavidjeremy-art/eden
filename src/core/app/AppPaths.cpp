#include "app/AppPaths.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

namespace eden {

AppPaths pathsUnder(const std::string& dataDirectory) {
    AppPaths paths;
    paths.dataDirectory = dataDirectory;
    paths.databasePath = dataDirectory + "/library.sqlite";
    paths.cacheDatabasePath = dataDirectory + "/ai-cache.sqlite";
    paths.imageDirectory = dataDirectory + "/images";
    paths.keyFilePath = dataDirectory + "/keys.json";
    paths.logPath = dataDirectory + "/eden.log";
    return paths;
}

AppPaths defaultPaths() {
#ifdef _WIN32
    std::string base;
    for (const char* name : {"LOCALAPPDATA", "APPDATA"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0') {
            base = value;
            break;
        }
    }
    if (base.empty()) {
        const char* profile = std::getenv("USERPROFILE");
        if (profile != nullptr && *profile != '\0') {
            base = std::string(profile) + "/AppData/Local";
        } else {
            std::error_code ignored;
            base = std::filesystem::temp_directory_path(ignored).generic_string();
        }
    }
    for (char& c : base) {
        if (c == '\\') c = '/';
    }
    return pathsUnder(base + "/eden-reader");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if (xdg != nullptr && *xdg != '\0') {
        base = xdg;
    } else if (home != nullptr && *home != '\0') {
        base = std::string(home) + "/.local/share";
    } else {
        base = "/tmp";
    }
    return pathsUnder(base + "/eden-reader");
#endif
}

Status ensureDirectories(const AppPaths& paths) {
    std::error_code error;
    std::filesystem::create_directories(paths.dataDirectory, error);
    if (error) return makeError(ErrorCode::Io, "cannot create " + paths.dataDirectory + ": " + error.message());
    std::filesystem::create_directories(paths.imageDirectory, error);
    if (error) return makeError(ErrorCode::Io, "cannot create " + paths.imageDirectory + ": " + error.message());
#ifndef _WIN32
    chmod(paths.dataDirectory.c_str(), 0700);
#endif
    return Status::success();
}

Result<std::string> makeTemporaryDirectory(const std::string& prefix) {
#ifdef _WIN32
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) return makeError(ErrorCode::Io, "no temporary directory: " + error.message());
    std::random_device device;
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 6; ++i) suffix.push_back(alphabet[device() % (sizeof(alphabet) - 1)]);
        const std::filesystem::path candidate = root / (prefix + suffix);
        if (std::filesystem::create_directory(candidate, error)) return candidate.generic_string();
        if (error) return makeError(ErrorCode::Io, "cannot create " + candidate.string() + ": " + error.message());
    }
    return makeError(ErrorCode::Io, "could not create a unique temporary directory");
#else
    const char* tmpdir = std::getenv("TMPDIR");  // Android has no /tmp; the app sets TMPDIR to its cache folder
    std::string pattern = std::string(tmpdir != nullptr && *tmpdir != '\0' ? tmpdir : "/tmp") + "/" + prefix + "XXXXXX";
    if (mkdtemp(pattern.data()) == nullptr) {
        return makeError(ErrorCode::Io, std::string("mkdtemp failed: ") + std::strerror(errno));
    }
    return pattern;
#endif
}

void removeDirectoryTree(const std::string& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

}  // namespace eden
