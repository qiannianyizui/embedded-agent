// tests/blackbox/test_property.cpp
// Property-based tests — verify invariants hold across a range of inputs.
// Uses Catch2 GENERATE and FuzzHelper for deterministic fuzzing.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "FuzzHelper.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "common/base/Error.h"
#include "common/base/Result.h"
#include "common/net/SseParser.h"
#include "nlohmann/json.hpp"

using namespace ea;
using namespace ea::agent;
using namespace ea::test;
using namespace ea::security;
using json = nlohmann::json;

// ── 1. AgentLoop never crashes on any input ────────────────────────────────────

TEST_CASE("Property: AgentLoop never crashes on any input", "[property][greybox]") {
    auto seed = GENERATE(1, 2, 3, 4, 5);
    FuzzGenerator fuzz(static_cast<uint64_t>(seed));

    auto mock = std::make_shared<MockProvider>();
    std::string random_text = fuzz.random_string(1, 200);
    mock->enqueue_text(random_text);

    ToolRegistry registry;
    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    AgentLoop loop(mock.get(), &registry, nullptr, AgentLoop::Config{},
                   [&](const std::string& t) { output = t; }, nullptr, &policy);

    std::string random_input = fuzz.random_string(1, 100);
    Result<void> result;
    // Must not crash on any input — verify via REQUIRE_NOTHROW
    REQUIRE_NOTHROW([&]() { result = loop.run(random_input); }());
    // Result type is always either ok or error (never throws)
    REQUIRE((result.ok() || result.error().code != ErrorCode{}));
}

// ── 2. Result is always ok or error ────────────────────────────────────────────

TEST_CASE("Property: Result<int> from value is ok", "[property][greybox]") {
    Result<int> r = 42;
    REQUIRE(r.ok());
    REQUIRE(r.value() == 42);
}

TEST_CASE("Property: Result<int> from Error is not ok", "[property][greybox]") {
    Result<int> r = Error::net("test error");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::NetworkError);
}

TEST_CASE("Property: Result<void> default is ok", "[property][greybox]") {
    Result<void> r;
    REQUIRE(r.ok());
}

TEST_CASE("Property: Result<void> from Error is not ok", "[property][greybox]") {
    Result<void> r = Error::timeout("test timeout");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::Timeout);
}

// ── 3. ToolRegistry.find returns nullptr for unknown tools ─────────────────────

TEST_CASE("Property: ToolRegistry.find returns nullptr for unknown tools", "[property][greybox]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("tool_a", "a"));
    registry.register_tool(std::make_unique<MockTool>("tool_b", "b"));
    registry.register_tool(std::make_unique<MockTool>("tool_c", "c"));

    auto seed = GENERATE(1, 2, 3);
    FuzzGenerator fuzz(static_cast<uint64_t>(seed));
    std::string random_name = "unknown_" + fuzz.random_string(3, 20);

    REQUIRE(registry.find(random_name) == nullptr);
}

// ── 4. SecurityPolicy.check_command returns Result for any string ───────────────

TEST_CASE("Property: SecurityPolicy.check_command returns Result for any string", "[property][greybox]") {
    auto seed = GENERATE(1, 2, 3, 4, 5);
    FuzzGenerator fuzz(static_cast<uint64_t>(seed));
    std::string random_cmd = fuzz.random_string(1, 50);

    // Test all AutonomyLevels
    SECTION("ReadOnly") {
        SecurityPolicy policy(AutonomyLevel::ReadOnly);
        auto result = policy.check_command(random_cmd);
        // ReadOnly blocks all commands — result should be error
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
    }
    SECTION("Supervised") {
        SecurityPolicy policy(AutonomyLevel::Supervised);
        Result<void> result;
        REQUIRE_NOTHROW(result = policy.check_command(random_cmd));
        // Supervised may allow or block depending on content
        if (!result.ok()) {
            REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
        }
    }
    SECTION("Full") {
        SecurityPolicy policy(AutonomyLevel::Full);
        auto result = policy.check_command(random_cmd);
        // Full allows everything
        REQUIRE(result.ok());
    }
}

// ── 5. Error factory methods produce correct ErrorCode ─────────────────────────

TEST_CASE("Property: Error::net produces NetworkError", "[property][greybox]") {
    auto e = Error::net("test");
    REQUIRE(e.code == ErrorCode::NetworkError);
}

TEST_CASE("Property: Error::timeout produces Timeout", "[property][greybox]") {
    auto e = Error::timeout("test");
    REQUIRE(e.code == ErrorCode::Timeout);
}

TEST_CASE("Property: Error::db produces DbError", "[property][greybox]") {
    auto e = Error::db("test");
    REQUIRE(e.code == ErrorCode::DbError);
}

TEST_CASE("Property: Error::security produces SecurityBlocked", "[property][greybox]") {
    auto e = Error::security("test");
    REQUIRE(e.code == ErrorCode::SecurityBlocked);
}

TEST_CASE("Property: Error::plugin produces PluginError", "[property][greybox]") {
    auto e = Error::plugin("test");
    REQUIRE(e.code == ErrorCode::PluginError);
}

// ── 6. JSON parse never crashes on arbitrary input ─────────────────────────────

TEST_CASE("Property: JSON parse never crashes on arbitrary input", "[property][greybox]") {
    auto seed = GENERATE(1, 2, 3, 4, 5);
    FuzzGenerator fuzz(static_cast<uint64_t>(seed));

    for (int i = 0; i < 10; ++i) {
        std::string random_str = fuzz.random_string(0, 256);
        try {
            json discarded = json::parse(random_str);
            (void)discarded;
        } catch (const json::exception&) {
            // Expected — invalid JSON should throw, not crash
        } catch (...) {
            // Any other exception is also acceptable (must not crash)
        }
    }
    // Reaching here means no crash — Catch2 will report crash as test failure
}

// ── 7. SseParser never crashes on arbitrary input ──────────────────────────────

TEST_CASE("Property: SseParser never crashes on arbitrary input", "[property][greybox]") {
    auto seed = GENERATE(1, 2, 3, 4, 5);
    FuzzGenerator fuzz(static_cast<uint64_t>(seed));

    net::SseParser parser;
    int event_count = 0;

    for (int i = 0; i < 5; ++i) {
        std::string random_sse = fuzz.random_sse_stream(5);
        try {
            parser.feed(random_sse, [&](const net::SseEvent&) {
                event_count++;
            });
        } catch (...) {
            // Must not crash — any exception is a bug
            FAIL("SseParser threw on random input");
        }
    }
    // Reaching here means no crash — Catch2 will report crash as test failure
}
