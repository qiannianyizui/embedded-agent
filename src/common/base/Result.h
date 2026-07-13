#pragma once
#include "Error.h"
#include <variant>
#include <optional>

namespace ea {

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}  // NOLINT
    Result(Error err) : data_(std::move(err)) {}   // NOLINT

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const & { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    T value_or(T default_val) const {
        if (ok()) return value();
        return default_val;
    }

    const Error& error() const { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(Error err) : error_(std::move(err)) {}  // NOLINT

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error& error() const { return *error_; }

private:
    std::optional<Error> error_;
};

}  // namespace ea
