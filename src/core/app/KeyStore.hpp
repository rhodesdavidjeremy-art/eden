#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "common/Result.hpp"

namespace eden {

class KeyStore {
public:
    virtual ~KeyStore() = default;
    virtual Result<std::optional<std::string>> get(const std::string& name) = 0;
    virtual Status set(const std::string& name, const std::string& key) = 0;
    virtual Status remove(const std::string& name) = 0;
    virtual std::string backendName() const = 0;
};

class FileKeyStore : public KeyStore {
public:
    explicit FileKeyStore(std::string path) : path_(std::move(path)) {}
    Result<std::optional<std::string>> get(const std::string& name) override;
    Status set(const std::string& name, const std::string& key) override;
    Status remove(const std::string& name) override;
    std::string backendName() const override { return "file"; }

private:
    Result<std::string> load();
    Status save(const std::string& content);

    std::string path_;
    std::mutex mutex_;
};

class SecretToolKeyStore : public KeyStore {
public:
    explicit SecretToolKeyStore(std::string tool = "secret-tool", std::string service = "eden-reader")
        : tool_(std::move(tool)), service_(std::move(service)) {}
    Result<std::optional<std::string>> get(const std::string& name) override;
    Status set(const std::string& name, const std::string& key) override;
    Status remove(const std::string& name) override;
    std::string backendName() const override { return "secret-service"; }

    static bool available(const std::string& tool = "secret-tool");

private:
    std::string tool_;
    std::string service_;
};

std::unique_ptr<KeyStore> makeDefaultKeyStore(const std::string& fallbackFile);

}  // namespace eden
