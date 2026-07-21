// tests/security/test_fuzz_json.cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "FuzzHelper.h"
#include "SecurityTestHelper.h"

using namespace ea::test;
using json = nlohmann::json;

TEST_CASE("Fuzz: JSON parsing handles malformed input without crashing", "[fuzz][security]") {
    FuzzGenerator gen(42);
    for (int i = 0; i < 1000; ++i) {
        std::string input = gen.random_json(5);
        // Must not crash
        auto result = json::parse(input, nullptr, false);
        // Parse failure is OK, crash is not
        (void)result;
    }
    REQUIRE(true);  // Reaching here means no crash
}

TEST_CASE("Fuzz: JSON parsing handles very deep nesting", "[fuzz][security]") {
    FuzzGenerator gen(123);
    for (int i = 0; i < 100; ++i) {
        std::string input = gen.random_json(20);  // depth 20
        auto result = json::parse(input, nullptr, false);
        // Must not crash (may fail due to depth limit, which is OK)
        (void)result;
    }
    REQUIRE(true);
}

TEST_CASE("Fuzz: JSON parsing handles random strings", "[fuzz][security]") {
    FuzzGenerator gen(456);
    for (int i = 0; i < 500; ++i) {
        std::string input = gen.random_string(0, 4096);
        auto result = json::parse(input, nullptr, false);
        // Must not crash
        (void)result;
    }
    REQUIRE(true);
}

TEST_CASE("Fuzz: JSON parsing handles injection payloads", "[fuzz][security]") {
    auto payloads = InjectionPayloads::json_injections();
    for (const auto& payload : payloads) {
        auto result = json::parse(payload, nullptr, false);
        // Must not crash
        (void)result;
    }
    REQUIRE(true);
}
