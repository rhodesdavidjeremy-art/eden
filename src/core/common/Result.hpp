#pragma once

#include <string>
#include <utility>
#include <variant>

namespace eden {

enum class ErrorCode {
    None,
    InvalidArgument,
    NotFound,
    Conflict,
    Immutable,
    Io,
    Database,
    Parse,
    Unsupported,
    Network,
    Timeout,
    Cancelled,
    Auth,
    RateLimited,
    Provider,
    Internal
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
};

inline Error makeError(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

inline const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::None: return "none";
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::NotFound: return "not_found";
        case ErrorCode::Conflict: return "conflict";
        case ErrorCode::Immutable: return "immutable";
        case ErrorCode::Io: return "io";
        case ErrorCode::Database: return "database";
        case ErrorCode::Parse: return "parse";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::Network: return "network";
        case ErrorCode::Timeout: return "timeout";
        case ErrorCode::Cancelled: return "cancelled";
        case ErrorCode::Auth: return "auth";
        case ErrorCode::RateLimited: return "rate_limited";
        case ErrorCode::Provider: return "provider";
        case ErrorCode::Internal: return "internal";
    }
    return "unknown";
}

template <typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    T& value() & { return std::get<T>(data_); }
    const T& value() const& { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const { return std::get<Error>(data_); }

    T valueOr(T fallback) const& {
        if (ok()) return std::get<T>(data_);
        return fallback;
    }

private:
    std::variant<T, Error> data_;
};

class Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)), ok_(false) {}

    static Status success() { return Status(); }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }

private:
    Error error_;
    bool ok_ = true;
};

}  // namespace eden
