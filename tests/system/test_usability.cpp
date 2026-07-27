// tests/system/test_usability.cpp
// Usability tests — validate that core APIs are easy to use correctly
// and that error messages contain useful context.
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "security/SecurityPolicy.h"
#include "base/Result.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;
using namespace ea::memory;
using namespace ea::security;

TEST_CASE("Usability: AgentLoop can be created and run with minimal setup", "[usability][system]") {
    MockProvider provider;
    provider.enqueue_text("Hello!");
    tool::ToolRegistry registry;
    std::string output;
    AgentLoop loop(&provider, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });
    auto result = loop.run("Hi");
    REQUIRE(result.ok());
}

TEST_CASE("Usability: Error messages contain useful context", "[usability]") {
    auto err = Error::net("connection refused to api.openai.com:443");
    REQUIRE(err.message.find("openai.com") != std::string::npos);
    REQUIRE(err.code == ErrorCode::NetworkError);

    auto sec_err = Error::security("dangerous command blocked: rm -rf /");
    REQUIRE(sec_err.message.find("rm") != std::string::npos);
    REQUIRE(sec_err.code == ErrorCode::SecurityBlocked);
}

TEST_CASE("Usability: SecurityPolicy easy to configure", "[usability]") {
    // 3 lines to configure a safe policy
    security::SecurityPolicy policy(security::AutonomyLevel::Supervised);
    policy.set_workspace("/workspace");
    policy.set_allowed_commands({"ls", "cat", "grep"});
    REQUIRE(policy.level() == security::AutonomyLevel::Supervised);
}

TEST_CASE("Usability: ToolRegistry easy to populate", "[usability]") {
    tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("tool1"));
    registry.register_tool(std::make_unique<MockTool>("tool2"));
    // Simple registration and lookup
    REQUIRE(registry.find("tool1") != nullptr);
    REQUIRE(registry.find("tool2") != nullptr);
}

TEST_CASE("Usability: InMemoryBackend works out of the box", "[usability]") {
    auto mem = std::make_unique<memory::InMemoryBackend>();
    mem->store("fact", "core", 7);
    auto results = mem->recall("fact", 5);
    REQUIRE(results.ok());
    REQUIRE_FALSE(results.value().empty());
}
