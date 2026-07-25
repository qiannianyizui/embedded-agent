#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <iostream>
#include "app/CliCommandRegistry.h"

using namespace ea::app;

TEST_CASE("CliCommandRegistry dispatches registered command", "[app]") {
    CliCommandRegistry reg;
    bool executed = false;
    std::string captured_args;

    reg.register_command({"test", "/test", "Test command",
        [&](const std::string& args) { executed = true; captured_args = args; }});

    REQUIRE(reg.try_dispatch("/test"));
    REQUIRE(executed);
    REQUIRE(captured_args == "");
}

TEST_CASE("CliCommandRegistry dispatches command with args", "[app]") {
    CliCommandRegistry reg;
    std::string captured_args;

    reg.register_command({"resume", "/resume <id>", "Resume conversation",
        [&](const std::string& args) { captured_args = args; }});

    REQUIRE(reg.try_dispatch("/resume abc123"));
    REQUIRE(captured_args == "abc123");
}

TEST_CASE("CliCommandRegistry returns false for non-command input", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"test", "/test", "Test command",
        [](const std::string&) {}});

    REQUIRE_FALSE(reg.try_dispatch("hello world"));
    REQUIRE_FALSE(reg.try_dispatch(""));
}

TEST_CASE("CliCommandRegistry returns false for unknown command", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"test", "/test", "Test command",
        [](const std::string&) {}});

    REQUIRE_FALSE(reg.try_dispatch("/unknown"));
}

TEST_CASE("CliCommandRegistry print_help lists commands", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"usage", "/usage", "Show usage", [](const std::string&) {}});
    reg.register_command({"cost", "/cost", "Show cost", [](const std::string&) {}});

    // Redirect cout
    std::ostringstream oss;
    auto* old_buf = std::cout.rdbuf(oss.rdbuf());
    reg.print_help();
    std::cout.rdbuf(old_buf);

    std::string output = oss.str();
    REQUIRE(output.find("/usage") != std::string::npos);
    REQUIRE(output.find("Show usage") != std::string::npos);
    REQUIRE(output.find("/cost") != std::string::npos);
    REQUIRE(output.find("Show cost") != std::string::npos);
}
