#include <catch2/catch_test_macros.hpp>
#include "provider/ErrorClassifier.h"
#include "base/Error.h"

using namespace ea;
using namespace ea::provider;

TEST_CASE("classify 429 as RateLimit", "[error_classifier]") {
    Error err = Error::rate_limit("slow down");
    err.http_status = 429;
    REQUIRE(classify_error(err) == ErrorClass::RateLimit);
}

TEST_CASE("classify 5xx as Retryable", "[error_classifier]") {
    Error err = Error::net("server error", 500);
    REQUIRE(classify_error(err) == ErrorClass::Retryable);

    Error err2 = Error::net("bad gateway", 502);
    REQUIRE(classify_error(err2) == ErrorClass::Retryable);

    Error err3 = Error::net("service unavailable", 503);
    REQUIRE(classify_error(err3) == ErrorClass::Retryable);
}

TEST_CASE("classify timeout as Retryable", "[error_classifier]") {
    Error err = Error::timeout("connection timed out");
    REQUIRE(classify_error(err) == ErrorClass::Retryable);
}

TEST_CASE("classify 4xx (not 429) as NonRetryable", "[error_classifier]") {
    Error err = Error::net("bad request", 400);
    REQUIRE(classify_error(err) == ErrorClass::NonRetryable);

    Error err2 = Error::auth("invalid key");
    REQUIRE(classify_error(err2) == ErrorClass::NonRetryable);

    Error err3 = Error::net("not found", 404);
    REQUIRE(classify_error(err3) == ErrorClass::NonRetryable);
}

TEST_CASE("classify parse error as NonRetryable", "[error_classifier]") {
    Error err = Error::parse("invalid JSON");
    REQUIRE(classify_error(err) == ErrorClass::NonRetryable);
}

TEST_CASE("classify security error as NonRetryable", "[error_classifier]") {
    Error err = Error::security("blocked");
    REQUIRE(classify_error(err) == ErrorClass::NonRetryable);
}
