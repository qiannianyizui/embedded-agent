// tests/system/test_smoke.cpp
// Smoke tests — validate that core paths work without crashing
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "Fixtures.h"
#include "memory/HolographicMemory.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "agent/AgentLoop.h"
#include "security/SecurityPolicy.h"
#include "config/Config.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;
using namespace ea::memory;
using namespace ea::security;

TEST_CASE("Smoke: AgentLoop basic conversation", "[smoke][system]") {
    MockProvider provider;
    provider.enqueue_text("Hello! How can I help?");

    tool::ToolRegistry registry;
    std::string output;
    AgentLoop loop(&provider, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("Hi");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello! How can I help?");
}

TEST_CASE("Smoke: Tool execution round-trip", "[smoke][system]") {
    MockProvider provider;
    provider.enqueue(make_tools({make_call("echo", "c1")}));
    provider.enqueue_text("Done");

    tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("echo", "echo result", false, false));

    std::string output;
    AgentLoop loop(&provider, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("use echo");
    REQUIRE(result.ok());
    REQUIRE(output == "Done");
}

TEST_CASE("Smoke: Memory store and recall", "[smoke][system]") {
    MemoryFixture f;
    f.in_memory->store("test fact", "core", 7);
    auto results = f.in_memory->recall("test fact", 5);
    REQUIRE(results.ok());
    REQUIRE_FALSE(results.value().empty());
    REQUIRE(results.value()[0].content == "test fact");
}

TEST_CASE("Smoke: SecurityPolicy default is Supervised", "[smoke][system]") {
    SecurityPolicy policy;
    REQUIRE(policy.level() == AutonomyLevel::Supervised);
}

TEST_CASE("Smoke: SecurityPolicy blocks dangerous commands", "[smoke][system]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    auto result = policy.check_command("rm -rf /");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Smoke: Config default construction", "[smoke][system]") {
    // Verify the config system does not crash
    config::AppConfig cfg;
    // Default config should be constructible
    REQUIRE(cfg.provider.type == "openai_compatible");
    REQUIRE(cfg.security.autonomy == "supervised");
}
