#include <catch2/catch_test_macros.hpp>
#include "net/RetryPolicy.h"

using namespace ea::net;

TEST_CASE("RetryPolicy should_retry on 429", "[retry]") {
    RetryPolicy policy;
    REQUIRE(policy.should_retry(429));
}

TEST_CASE("RetryPolicy should_retry on 5xx", "[retry]") {
    RetryPolicy policy;
    REQUIRE(policy.should_retry(500));
    REQUIRE(policy.should_retry(502));
    REQUIRE(policy.should_retry(503));
    REQUIRE(policy.should_retry(599));
}

TEST_CASE("RetryPolicy should not retry on 4xx (not 429)", "[retry]") {
    RetryPolicy policy;
    REQUIRE_FALSE(policy.should_retry(400));
    REQUIRE_FALSE(policy.should_retry(401));
    REQUIRE_FALSE(policy.should_retry(403));
    REQUIRE_FALSE(policy.should_retry(404));
}

TEST_CASE("RetryPolicy should not retry on 2xx", "[retry]") {
    RetryPolicy policy;
    REQUIRE_FALSE(policy.should_retry(200));
    REQUIRE_FALSE(policy.should_retry(201));
    REQUIRE_FALSE(policy.should_retry(204));
}

TEST_CASE("RetryPolicy should not retry on 3xx", "[retry]") {
    RetryPolicy policy;
    REQUIRE_FALSE(policy.should_retry(301));
    REQUIRE_FALSE(policy.should_retry(302));
}

TEST_CASE("RetryPolicy delay_for exponential backoff", "[retry]") {
    RetryPolicy policy;
    policy.base_delay = std::chrono::milliseconds(1000);
    policy.backoff_multiplier = 2.0;

    REQUIRE(policy.delay_for(0) == std::chrono::milliseconds(1000));
    REQUIRE(policy.delay_for(1) == std::chrono::milliseconds(2000));
    REQUIRE(policy.delay_for(2) == std::chrono::milliseconds(4000));
    REQUIRE(policy.delay_for(3) == std::chrono::milliseconds(8000));
}

TEST_CASE("RetryPolicy delay_for capped by max_delay", "[retry]") {
    RetryPolicy policy;
    policy.base_delay = std::chrono::milliseconds(1000);
    policy.backoff_multiplier = 2.0;
    policy.max_delay = std::chrono::milliseconds(5000);

    REQUIRE(policy.delay_for(0) == std::chrono::milliseconds(1000));
    REQUIRE(policy.delay_for(1) == std::chrono::milliseconds(2000));
    REQUIRE(policy.delay_for(2) == std::chrono::milliseconds(4000));
    REQUIRE(policy.delay_for(3) == std::chrono::milliseconds(5000));  // capped
    REQUIRE(policy.delay_for(10) == std::chrono::milliseconds(5000)); // still capped
}

TEST_CASE("RetryPolicy default values", "[retry]") {
    RetryPolicy policy;
    REQUIRE(policy.max_retries == 3);
    REQUIRE(policy.base_delay == std::chrono::milliseconds(1000));
    REQUIRE(policy.backoff_multiplier == 2.0);
    REQUIRE(policy.max_delay == std::chrono::milliseconds(30000));
}
