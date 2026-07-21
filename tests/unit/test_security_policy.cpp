#include <catch2/catch_test_macros.hpp>
#include "security/SecurityPolicy.h"

using namespace ea;
using namespace ea::security;

TEST_CASE("SecurityPolicy Full autonomy allows all commands", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Full);
    REQUIRE(policy.check_command("rm -rf /").ok());
    REQUIRE(policy.check_command("anything").ok());
}

TEST_CASE("SecurityPolicy Full autonomy allows all file paths", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Full);
    REQUIRE(policy.check_file_path("/etc/passwd").ok());
    REQUIRE(policy.check_file_path("/any/path").ok());
}

TEST_CASE("SecurityPolicy Full autonomy allows all tools", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Full);
    REQUIRE(policy.check_tool("shell").ok());
    REQUIRE(policy.check_tool("file").ok());
}

TEST_CASE("SecurityPolicy ReadOnly blocks all commands", "[security]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    auto result = policy.check_command("ls");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

TEST_CASE("SecurityPolicy ReadOnly blocks all file access", "[security]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    auto result = policy.check_file_path("/tmp/test");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

TEST_CASE("SecurityPolicy ReadOnly allows read-only tools", "[security]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    REQUIRE(policy.check_tool("search_files").ok());
    REQUIRE(policy.check_tool("web").ok());
}

TEST_CASE("SecurityPolicy ReadOnly blocks non-read-only tools", "[security]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);
    auto result = policy.check_tool("shell");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

TEST_CASE("SecurityPolicy Supervised blocks dangerous commands", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    REQUIRE_FALSE(policy.check_command("rm -rf /").ok());
    REQUIRE_FALSE(policy.check_command("shutdown").ok());
    REQUIRE_FALSE(policy.check_command("reboot").ok());
    REQUIRE_FALSE(policy.check_command("mkfs something").ok());
}

TEST_CASE("SecurityPolicy Supervised allows safe commands", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    REQUIRE(policy.check_command("ls -la").ok());
    REQUIRE(policy.check_command("echo hello").ok());
    REQUIRE(policy.check_command("cat file.txt").ok());
}

TEST_CASE("SecurityPolicy Supervised command whitelist", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_allowed_commands({"ls", "cat", "echo"});

    REQUIRE(policy.check_command("ls -la").ok());
    REQUIRE(policy.check_command("cat file.txt").ok());
    REQUIRE(policy.check_command("echo hello").ok());

    auto result = policy.check_command("rm file.txt");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

TEST_CASE("SecurityPolicy Supervised whitelist extracts base command from path", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_allowed_commands({"git"});

    REQUIRE(policy.check_command("/usr/bin/git status").ok());
}

TEST_CASE("SecurityPolicy Supervised dangerous commands blocked even with whitelist", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_allowed_commands({"rm"});

    // Dangerous commands are always blocked in Supervised mode
    REQUIRE_FALSE(policy.check_command("rm -rf /").ok());
}

TEST_CASE("SecurityPolicy Supervised file path within workspace", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_workspace("/home/user/project");

    REQUIRE(policy.check_file_path("/home/user/project/file.txt").ok());
    REQUIRE(policy.check_file_path("/home/user/project/sub/deep.txt").ok());
}

TEST_CASE("SecurityPolicy Supervised file path outside workspace blocked", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    policy.set_workspace("/home/user/project");

    auto result = policy.check_file_path("/etc/passwd");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

TEST_CASE("SecurityPolicy Supervised no workspace allows all paths", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    REQUIRE(policy.check_file_path("/any/path").ok());
}

TEST_CASE("SecurityPolicy Supervised allows all tools", "[security]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);
    REQUIRE(policy.check_tool("shell").ok());
    REQUIRE(policy.check_tool("file").ok());
    REQUIRE(policy.check_tool("search_files").ok());
}

TEST_CASE("SecurityPolicy level accessor", "[security]") {
    SecurityPolicy p1(AutonomyLevel::ReadOnly);
    REQUIRE(p1.level() == AutonomyLevel::ReadOnly);

    SecurityPolicy p2(AutonomyLevel::Supervised);
    REQUIRE(p2.level() == AutonomyLevel::Supervised);

    SecurityPolicy p3(AutonomyLevel::Full);
    REQUIRE(p3.level() == AutonomyLevel::Full);
}

TEST_CASE("SecurityPolicy default is Supervised", "[security]") {
    SecurityPolicy policy;
    REQUIRE(policy.level() == AutonomyLevel::Supervised);
}
