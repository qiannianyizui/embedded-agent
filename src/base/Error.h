#pragma once
#include <string>

namespace ea {

enum class ErrorCode {
    Unknown,
    NetworkError,
    Timeout,
    AuthError,
    RateLimit,
    InvalidArgument,
    NotFound,
    IoError,
    DbError,
    ToolError,
    ParseError,
    SecurityBlocked,
    ConfigError,
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    int http_status = 0;
    std::string detail;

    static Error net(const std::string& msg, int status = 0) {
        return {ErrorCode::NetworkError, msg, status, {}};
    }
    static Error timeout(const std::string& msg) {
        return {ErrorCode::Timeout, msg, 0, {}};
    }
    static Error auth(const std::string& msg) {
        return {ErrorCode::AuthError, msg, 0, {}};
    }
    static Error rate_limit(const std::string& msg) {
        return {ErrorCode::RateLimit, msg, 0, {}};
    }
    static Error db(const std::string& msg) {
        return {ErrorCode::DbError, msg, 0, {}};
    }
    static Error tool_error(const std::string& msg) {
        return {ErrorCode::ToolError, msg, 0, {}};
    }
    static Error not_found(const std::string& msg) {
        return {ErrorCode::NotFound, msg, 0, {}};
    }
    static Error parse(const std::string& msg) {
        return {ErrorCode::ParseError, msg, 0, {}};
    }
    static Error security(const std::string& msg) {
        return {ErrorCode::SecurityBlocked, msg, 0, {}};
    }
    static Error io(const std::string& msg) {
        return {ErrorCode::IoError, msg, 0, {}};
    }
    static Error invalid_arg(const std::string& msg) {
        return {ErrorCode::InvalidArgument, msg, 0, {}};
    }
    static Error config(const std::string& msg) {
        return {ErrorCode::ConfigError, msg, 0, {}};
    }
};

}  // namespace ea
