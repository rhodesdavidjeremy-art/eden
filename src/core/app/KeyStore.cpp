#include "app/KeyStore.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ai/ChildProcess.hpp"
#include "common/Json.hpp"
#include "common/Strings.hpp"

namespace eden {

Result<std::string> FileKeyStore::load() {
    std::ifstream input(path_, std::ios::binary);
    if (!input) return std::string("{}");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

Status FileKeyStore::save(const std::string& content) {
    const std::string temporary = path_ + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return makeError(ErrorCode::Io, "cannot write " + temporary);
#ifndef _WIN32
        chmod(temporary.c_str(), 0600);
#endif
        output << content;
        if (!output) return makeError(ErrorCode::Io, "write failure: " + temporary);
    }
#ifdef _WIN32
    // std::rename does not overwrite an existing file on Windows; std::filesystem::rename does.
    std::error_code renameError;
    std::filesystem::rename(temporary, path_, renameError);
    if (renameError) return makeError(ErrorCode::Io, "cannot replace " + path_ + ": " + renameError.message());
#else
    chmod(temporary.c_str(), 0600);
    if (std::rename(temporary.c_str(), path_.c_str()) != 0) return makeError(ErrorCode::Io, "cannot replace " + path_);
#endif
    return Status::success();
}

Result<std::optional<std::string>> FileKeyStore::get(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto content = load();
    if (!content.ok()) return content.error();
    auto parsed = Json::parse(content.value());
    if (!parsed.ok()) return parsed.error();
    if (!parsed.value().contains(name)) return std::optional<std::string>();
    return std::optional<std::string>(parsed.value().getString(name));
}

Status FileKeyStore::set(const std::string& name, const std::string& key) {
    if (name.empty()) return makeError(ErrorCode::InvalidArgument, "key name is empty");
    std::lock_guard<std::mutex> lock(mutex_);
    auto content = load();
    if (!content.ok()) return content.error();
    auto parsed = Json::parse(content.value());
    Json document = parsed.ok() && parsed.value().isObject() ? parsed.value() : Json::object();
    document[name] = key;
    return save(document.dump());
}

Status FileKeyStore::remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto content = load();
    if (!content.ok()) return content.error();
    auto parsed = Json::parse(content.value());
    if (!parsed.ok() || !parsed.value().isObject()) return Status::success();
    Json document = parsed.value();
    document.erase(name);
    return save(document.dump());
}

bool SecretToolKeyStore::available(const std::string& tool) {
#ifdef _WIN32
    (void)tool;  // secret-tool is a Linux (libsecret) utility; Windows uses the private key file.
    return false;
#else
    if (tool.find('/') != std::string::npos) return access(tool.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;
    for (const std::string& directory : split(path, ':', false)) {
        if (access((directory + "/" + tool).c_str(), X_OK) == 0) return true;
    }
    return false;
#endif
}

Result<std::optional<std::string>> SecretToolKeyStore::get(const std::string& name) {
    auto spawned = ChildProcess::spawn({tool_, "lookup", "service", service_, "account", name});
    if (!spawned.ok()) return spawned.error();
    std::string value;
    std::string line;
    bool first = true;
    while (spawned.value()->readLine(line, 5000) == ReadStatus::Line) {
        if (!first) value.push_back('\n');
        value += line;
        first = false;
    }
    const int code = spawned.value()->wait(5000);
    if (code != 0 || value.empty()) return std::optional<std::string>();
    return std::optional<std::string>(value);
}

Status SecretToolKeyStore::set(const std::string& name, const std::string& key) {
    if (name.empty() || key.empty()) return makeError(ErrorCode::InvalidArgument, "key name and value are required");
    auto spawned = ChildProcess::spawn({tool_, "store", "--label=Eden Research Reader: " + name, "service", service_,
                                        "account", name});
    if (!spawned.ok()) return spawned.error();
    spawned.value()->writeAll(key);
    spawned.value()->closeStdin();
    const int code = spawned.value()->wait(10000);
    if (code != 0) return makeError(ErrorCode::Io, "secret-tool could not store the key");
    return Status::success();
}

Status SecretToolKeyStore::remove(const std::string& name) {
    auto spawned = ChildProcess::spawn({tool_, "clear", "service", service_, "account", name});
    if (!spawned.ok()) return spawned.error();
    spawned.value()->wait(5000);
    return Status::success();
}

std::unique_ptr<KeyStore> makeDefaultKeyStore(const std::string& fallbackFile) {
    if (SecretToolKeyStore::available()) return std::make_unique<SecretToolKeyStore>();
    return std::make_unique<FileKeyStore>(fallbackFile);
}

}  // namespace eden
