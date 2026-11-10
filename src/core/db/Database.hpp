#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/Result.hpp"
#include "db/Statement.hpp"

namespace eden {

struct OpenOptions {
    bool readOnly = false;
    bool createIfMissing = true;
    bool walMode = true;
    bool foreignKeys = true;
    int busyTimeoutMs = 5000;
};

class Database {
public:
    static Database open(const std::string& path, const OpenOptions& options = {});
    static Database openMemory();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    ~Database();

    bool isOpen() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }
    sqlite3* handle() const { return db_; }

    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);

    std::int64_t lastInsertRowId() const;
    int changes() const;

    std::int64_t scalarInt(const std::string& sql, std::int64_t fallback = 0);
    std::optional<std::string> scalarText(const std::string& sql);

    std::int64_t userVersion();
    void setUserVersion(std::int64_t version);

    bool tableExists(const std::string& name);
    std::vector<std::string> tableNames();

    void checkpoint();
    void vacuum();
    void close();

    std::unique_lock<std::recursive_mutex> lock() const;

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
    int transactionDepth() const { return depth_; }

private:
    Database() = default;
    void configure(const OpenOptions& options);

    sqlite3* db_ = nullptr;
    std::string path_;
    int depth_ = 0;
    std::unique_ptr<std::recursive_mutex> mutex_ = std::make_unique<std::recursive_mutex>();
};

class Transaction {
public:
    explicit Transaction(Database& db);
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    void commit();
    void rollback();

private:
    Database& db_;
    bool finished_ = false;
};

template <typename T, typename F>
Result<T> guarded(F&& body) {
    try {
        return Result<T>(body());
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
}

template <typename F>
Status guardedStatus(F&& body) {
    try {
        body();
        return Status::success();
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
}

}  // namespace eden
