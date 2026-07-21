// tests/security/test_tool_isolation.cpp
#include <catch2/catch_test_macros.hpp>
#include "MockTool.h"
#include "security/SecurityPolicy.h"
#include "SecurityTestHelper.h"

using namespace ea;
using namespace ea::test;
using namespace ea::security;

TEST_CASE("Tool isolation: dangerous tool flagged correctly", "[security][tool]") {
    MockTool dangerous("nuke", "boom", true, true);
    REQUIRE(dangerous.is_dangerous() == true);
    REQUIRE(dangerous.is_mutating() == true);
}

TEST_CASE("Tool isolation: safe tool not flagged", "[security][tool]") {
    MockTool safe("search", "results", false, false);
    REQUIRE(safe.is_dangerous() == false);
    REQUIRE(safe.is_mutating() == false);
}

TEST_CASE("Tool isolation: ReadOnly blocks mutating tools", "[security][tool]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    // Mutating tools should be blocked under ReadOnly
    auto result = policy.check_tool("shell");
    REQUIRE_FALSE(result.ok());
    result = policy.check_tool("file");
    REQUIRE_FALSE(result.ok());
    result = policy.check_tool("memory");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Tool isolation: ReadOnly allows read-only tools", "[security][tool]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    auto result = policy.check_tool("search_files");
    REQUIRE(result.ok());
    result = policy.check_tool("web");
    REQUIRE(result.ok());
}

TEST_CASE("Tool isolation: Supervised allows all tools", "[security][tool]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    auto result = policy.check_tool("shell");
    REQUIRE(result.ok());
    result = policy.check_tool("file");
    REQUIRE(result.ok());
    result = policy.check_tool("memory");
    REQUIRE(result.ok());
}

TEST_CASE("Tool isolation: command injection with dangerous patterns blocked in Supervised",
          "[security][tool]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    // Verify that commands containing dangerous patterns from DANGEROUS_COMMANDS are blocked.
    // DANGEROUS_COMMANDS: "rm -rf /", "mkfs", "dd if=", "shutdown", "reboot",
    // "chmod -R 777 /", "curl | sh", "wget | sh", "systemctl stop", etc.
    REQUIRE_FALSE(policy.check_command("ls; rm -rf /").ok());
    REQUIRE_FALSE(policy.check_command("ls || shutdown -h now").ok());
    REQUIRE_FALSE(policy.check_command("mkfs /dev/sda1").ok());
    REQUIRE_FALSE(policy.check_command("dd if=/dev/zero of=/dev/sda").ok());
}

TEST_CASE("Tool isolation: path traversal payloads all blocked with workspace",
          "[security][tool]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_workspace("/workspace");
    auto payloads = InjectionPayloads::path_traversals();
    for (const auto& path : payloads) {
        auto result = policy.check_file_path(path);
        REQUIRE_FALSE(result.ok());
    }
}
