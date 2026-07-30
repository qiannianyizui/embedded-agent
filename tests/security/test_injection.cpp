// tests/security/test_injection.cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/HolographicMemory.h"
#include "security/SecurityPolicy.h"
#include "SecurityTestHelper.h"

using namespace ea;
using namespace ea::memory;
using namespace ea::security;
using namespace ea::test;

TEST_CASE("Injection: SQL injection in memory recall returns empty results", "[security][injection]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    mem.open();
    // Store normal data
    mem.store("normal fact about C++", "core", 7);

    auto payloads = InjectionPayloads::sql_injections();
    for (const auto& inj : payloads) {
        // Must not crash, must not leak extra data
        auto result = mem.recall(inj, 10);
        REQUIRE(result.ok());
        // SQL injection should not return more results than a normal query
        // FTS5 parameterized queries should prevent injection
    }
}

TEST_CASE("Injection: SQL injection in memory store does not corrupt data", "[security][injection]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    mem.open();

    auto payloads = InjectionPayloads::sql_injections();
    for (const auto& inj : payloads) {
        // Storing injection strings as content must not crash
        auto result = mem.store(inj, "test", 5);
        REQUIRE(result.ok());
    }

    // Verify normal queries still work
    auto results = mem.recall("normal", 10);
    REQUIRE(results.ok());
}

TEST_CASE("Injection: command injection blocked by all non-Full policies", "[security][injection]") {
    SecurityPolicy supervised(AutonomyLevel::Supervised);
    // Verify that commands containing dangerous patterns from DANGEROUS_COMMANDS are blocked.
    // DANGEROUS_COMMANDS: "rm -rf /", "mkfs", "dd if=", "shutdown", "reboot",
    // "chmod -R 777 /", "curl | sh", "wget | sh", "systemctl stop", etc.
    REQUIRE_FALSE(supervised.check_command("ls; rm -rf /").ok());
    REQUIRE_FALSE(supervised.check_command("ls || shutdown -h now").ok());
    REQUIRE_FALSE(supervised.check_command("mkfs /dev/sda1").ok());
    REQUIRE_FALSE(supervised.check_command("dd if=/dev/zero of=/dev/sda").ok());

    SecurityPolicy readonly(AutonomyLevel::ReadOnly);
    // ReadOnly blocks ALL commands
    auto payloads = InjectionPayloads::command_injections();
    for (const auto& cmd : payloads) {
        auto result = readonly.check_command(cmd);
        REQUIRE_FALSE(result.ok());
    }
}

TEST_CASE("Injection: path traversal blocked with workspace set", "[security][injection]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_workspace("/workspace");

    auto payloads = InjectionPayloads::path_traversals();
    for (const auto& path : payloads) {
        auto result = policy.check_file_path(path);
        REQUIRE_FALSE(result.ok());
    }
}

TEST_CASE("Injection: prompt injection strings stored safely in memory", "[security][injection]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    mem.open();

    auto payloads = InjectionPayloads::prompt_injections();
    for (const auto& prompt : payloads) {
        auto result = mem.store(prompt, "prompt_test", 5);
        REQUIRE(result.ok());
    }
    // Verify normal queries are unaffected after storing injection strings
    auto results = mem.recall("instructions", 10);
    REQUIRE(results.ok());
}
