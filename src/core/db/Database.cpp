#include "db/Database.hpp"

#include <sqlite3.h>

#include <utility>

namespace eden {

namespace {

[[noreturn]] void raiseDb(sqlite3* db, int code, const std::string& context) {
    std::string message = context;
    if (db != nullptr) {
        message += ": ";
        message += sqlite3_errmsg(db);
    }
    throw DbError(code, message);
}

}  // namespace

Database Database::open(const std::string& path, const OpenOptions& options) {
    Database database;
    int flags = SQLITE_OPEN_FULLMUTEX;
    if (options.readOnly) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE;
        if (options.createIfMissing) flags |= SQLITE_OPEN_CREATE;
    }
    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = "cannot open database " + path;
        if (handle != nullptr) {
            message += ": ";
            message += sqlite3_errmsg(handle);
            sqlite3_close(handle);
        }
        throw DbError(rc, message);
    }
    database.db_ = handle;
    database.path_ = path;
    database.configure(options);
    return database;
}

Database Database::openMemory() {
    OpenOptions options;
    options.walMode = false;
    return open(":memory:", options);
}

void Database::configure(const OpenOptions& options) {
    sqlite3_busy_timeout(db_, options.busyTimeoutMs);
    if (options.foreignKeys) exec("PRAGMA foreign_keys = ON");
    if (!options.readOnly && options.walMode) {
        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous = NORMAL");
    }
    exec("PRAGMA temp_store = MEMORY");
}

Database::Database(Database&& other) noexcept
    : db_(other.db_),
      path_(std::move(other.path_)),
      depth_(other.depth_),
      mutex_(std::move(other.mutex_)) {
    other.db_ = nullptr;
    other.depth_ = 0;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        path_ = std::move(other.path_);
        depth_ = other.depth_;
        mutex_ = std::move(other.mutex_);
        other.db_ = nullptr;
        other.depth_ = 0;
    }
    return *this;
}

Database::~Database() { close(); }

void Database::close() {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

std::unique_lock<std::recursive_mutex> Database::lock() const {
    return std::unique_lock<std::recursive_mutex>(*mutex_);
}

void Database::exec(const std::string& sql) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string text = message != nullptr ? message : "unknown error";
        sqlite3_free(message);
        throw DbError(rc, "exec failed: " + text);
    }
}

Statement Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) raiseDb(db_, rc, "prepare failed for: " + sql);
    return Statement(db_, stmt);
}

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

int Database::changes() const { return sqlite3_changes(db_); }

std::int64_t Database::scalarInt(const std::string& sql, std::int64_t fallback) {
    Statement stmt = prepare(sql);
    if (stmt.step()) return stmt.getInt(0);
    return fallback;
}

std::optional<std::string> Database::scalarText(const std::string& sql) {
    Statement stmt = prepare(sql);
    if (stmt.step() && !stmt.isNull(0)) return stmt.getText(0);
    return std::nullopt;
}

std::int64_t Database::userVersion() { return scalarInt("PRAGMA user_version"); }

void Database::setUserVersion(std::int64_t version) {
    exec("PRAGMA user_version = " + std::to_string(version));
}

bool Database::tableExists(const std::string& name) {
    Statement stmt = prepare("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name = ?1");
    stmt.bindText(1, name);
    return stmt.step();
}

std::vector<std::string> Database::tableNames() {
    std::vector<std::string> names;
    Statement stmt = prepare("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name");
    while (stmt.step()) names.push_back(stmt.getText(0));
    return names;
}

void Database::checkpoint() { exec("PRAGMA wal_checkpoint(TRUNCATE)"); }

void Database::vacuum() { exec("VACUUM"); }

void Database::beginTransaction() {
    if (depth_ == 0) {
        exec("BEGIN IMMEDIATE");
    } else {
        exec("SAVEPOINT eden_sp_" + std::to_string(depth_));
    }
    ++depth_;
}

void Database::commitTransaction() {
    if (depth_ == 0) throw DbError(SQLITE_MISUSE, "no active transaction");
    --depth_;
    if (depth_ == 0) {
        exec("COMMIT");
    } else {
        exec("RELEASE SAVEPOINT eden_sp_" + std::to_string(depth_));
    }
}

void Database::rollbackTransaction() {
    if (depth_ == 0) throw DbError(SQLITE_MISUSE, "no active transaction");
    --depth_;
    if (depth_ == 0) {
        exec("ROLLBACK");
    } else {
        const std::string name = "eden_sp_" + std::to_string(depth_);
        exec("ROLLBACK TO SAVEPOINT " + name);
        exec("RELEASE SAVEPOINT " + name);
    }
}

Transaction::Transaction(Database& db) : db_(db) { db_.beginTransaction(); }

Transaction::~Transaction() {
    if (!finished_) {
        try {
            db_.rollbackTransaction();
        } catch (...) {
        }
    }
}

void Transaction::commit() {
    if (finished_) throw DbError(SQLITE_MISUSE, "transaction already finished");
    finished_ = true;
    db_.commitTransaction();
}

void Transaction::rollback() {
    if (finished_) throw DbError(SQLITE_MISUSE, "transaction already finished");
    finished_ = true;
    db_.rollbackTransaction();
}

}  // namespace eden
