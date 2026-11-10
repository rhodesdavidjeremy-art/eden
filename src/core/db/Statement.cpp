#include "db/Statement.hpp"

#include <sqlite3.h>

#include <utility>

namespace eden {

Statement::Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        release();
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

Statement::~Statement() { release(); }

void Statement::release() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

void Statement::raise(int code, const char* context) const {
    std::string message = context;
    if (db_ != nullptr) {
        message += ": ";
        message += sqlite3_errmsg(db_);
    }
    throw DbError(code, message);
}

int Statement::parameterIndex(std::string_view name) const {
    const std::string key(name);
    const int index = sqlite3_bind_parameter_index(stmt_, key.c_str());
    if (index == 0) throw DbError(SQLITE_RANGE, "unknown bind parameter: " + key);
    return index;
}

Statement& Statement::bindNull(int index) {
    const int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) raise(rc, "bind null");
    return *this;
}

Statement& Statement::bindInt(int index, std::int64_t value) {
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) raise(rc, "bind integer");
    return *this;
}

Statement& Statement::bindDouble(int index, double value) {
    const int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK) raise(rc, "bind real");
    return *this;
}

Statement& Statement::bindText(int index, std::string_view value) {
    const int rc = sqlite3_bind_text64(stmt_, index, value.data(),
                                       static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT,
                                       SQLITE_UTF8);
    if (rc != SQLITE_OK) raise(rc, "bind text");
    return *this;
}

Statement& Statement::bindBlob(int index, const Bytes& value) {
    const int rc = sqlite3_bind_blob64(stmt_, index, value.data(),
                                       static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) raise(rc, "bind blob");
    return *this;
}

Statement& Statement::bindNull(std::string_view name) { return bindNull(parameterIndex(name)); }

Statement& Statement::bindInt(std::string_view name, std::int64_t value) {
    return bindInt(parameterIndex(name), value);
}

Statement& Statement::bindDouble(std::string_view name, double value) {
    return bindDouble(parameterIndex(name), value);
}

Statement& Statement::bindText(std::string_view name, std::string_view value) {
    return bindText(parameterIndex(name), value);
}

Statement& Statement::bindBlob(std::string_view name, const Bytes& value) {
    return bindBlob(parameterIndex(name), value);
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    raise(rc, "step");
}

void Statement::execute() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
}

void Statement::clearBindings() {
    sqlite3_clear_bindings(stmt_);
}

int Statement::columnCount() const { return sqlite3_column_count(stmt_); }

std::string Statement::columnName(int index) const {
    const char* name = sqlite3_column_name(stmt_, index);
    return name == nullptr ? std::string() : std::string(name);
}

int Statement::columnIndex(std::string_view name) const {
    const int count = columnCount();
    for (int i = 0; i < count; ++i) {
        if (name == sqlite3_column_name(stmt_, i)) return i;
    }
    throw DbError(SQLITE_RANGE, "unknown column: " + std::string(name));
}

bool Statement::isNull(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::int64_t Statement::getInt(int index) const { return sqlite3_column_int64(stmt_, index); }

double Statement::getDouble(int index) const { return sqlite3_column_double(stmt_, index); }

std::string Statement::getText(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    if (text == nullptr) return std::string();
    const int length = sqlite3_column_bytes(stmt_, index);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(length));
}

Bytes Statement::getBlob(int index) const {
    const void* data = sqlite3_column_blob(stmt_, index);
    const int length = sqlite3_column_bytes(stmt_, index);
    if (data == nullptr || length <= 0) return Bytes();
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    return Bytes(bytes, bytes + length);
}

}  // namespace eden
