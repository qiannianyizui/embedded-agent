#include <catch2/catch_test_macros.hpp>
#include "common/base/Result.h"

using namespace ea;

TEST_CASE("Result<T> stores value", "[result]") {
    Result<int> r = 42;
    REQUIRE(r.ok());
    REQUIRE(r.value() == 42);
}

TEST_CASE("Result<T> stores error", "[result]") {
    Result<int> r = Error{ErrorCode::NetworkError, "connection failed"};
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::NetworkError);
    REQUIRE(r.error().message == "connection failed");
}

TEST_CASE("Result<T> value_or returns value when ok", "[result]") {
    Result<int> r = 42;
    REQUIRE(r.value_or(0) == 42);
}

TEST_CASE("Result<T> value_or returns default when error", "[result]") {
    Result<int> r = Error{ErrorCode::Unknown, ""};
    REQUIRE(r.value_or(99) == 99);
}

TEST_CASE("Result<void> success", "[result]") {
    Result<void> r;
    REQUIRE(r.ok());
}

TEST_CASE("Result<void> error", "[result]") {
    Result<void> r = Error{ErrorCode::DbError, "query failed"};
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::DbError);
}

TEST_CASE("Error factory methods", "[result]") {
    auto e1 = Error::net("timeout", 504);
    REQUIRE(e1.code == ErrorCode::NetworkError);
    REQUIRE(e1.http_status == 504);

    auto e2 = Error::auth("bad key");
    REQUIRE(e2.code == ErrorCode::AuthError);

    auto e3 = Error::rate_limit("slow down");
    REQUIRE(e3.code == ErrorCode::RateLimit);
}
