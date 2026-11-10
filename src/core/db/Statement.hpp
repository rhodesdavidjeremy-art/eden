#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common/Types.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace eden {

class DbError : public std::runtime_error {
public:
    DbError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}
    int code() const { return code_; }

private:
    int code_;
};

class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, sqlite3_stmt* stmt);
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    ~Statement();

    bool valid() const { return stmt_ != nullptr; }

    Statement& bindNull(int index);
    Statement& bindInt(int index, std::int64_t value);
    Statement& bindDouble(int index, double value);
    Statement& bindText(int index, std::string_view value);
    Statement& bindBlob(int index, const Bytes& value);

    Statement& bindNull(std::string_view name);
    Statement& bindInt(std::string_view name, std::int64_t value);
    Statement& bindDouble(std::string_view name, double value);
    Statement& bindText(std::string_view name, std::string_view value);
    Statement& bindBlob(std::string_view name, const Bytes& value);

    int parameterIndex(std::string_view name) const;

    bool step();
    void execute();
    void reset();
    void clearBindings();

    int columnCount() const;
    std::string columnName(int index) const;
    int columnIndex(std::string_view name) const;
    bool isNull(int index) const;
    std::int64_t getInt(int index) const;
    double getDouble(int index) const;
    std::string getText(int index) const;
    Bytes getBlob(int index) const;

    std::int64_t getInt(std::string_view name) const { return getInt(columnIndex(name)); }
    double getDouble(std::string_view name) const { return getDouble(columnIndex(name)); }
    std::string getText(std::string_view name) const { return getText(columnIndex(name)); }
    bool isNull(std::string_view name) const { return isNull(columnIndex(name)); }

private:
    void release();
    [[noreturn]] void raise(int code, const char* context) const;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace eden
