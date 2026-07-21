#include <catch2/catch_test_macros.hpp>
#include "tool/ToolOutputConfig.h"

using namespace ea::tool;

TEST_CASE("truncate_output returns original when under limit", "[tool_output]") {
    std::string output = "hello world";
    auto result = truncate_output(output, 100, "\n...[truncated]");
    REQUIRE(result == "hello world");
}

TEST_CASE("truncate_output truncates when over limit", "[tool_output]") {
    std::string output(200, 'x');
    auto result = truncate_output(output, 100, "\n...[truncated]");
    REQUIRE(result.size() < 200);
    REQUIRE(result.find("...[truncated]") != std::string::npos);
}

TEST_CASE("truncate_output exact limit is not truncated", "[tool_output]") {
    std::string output(100, 'x');
    auto result = truncate_output(output, 100, "\n...[truncated]");
    REQUIRE(result.size() == 100);
    REQUIRE(result.find("...[truncated]") == std::string::npos);
}

TEST_CASE("truncate_output empty string returns empty", "[tool_output]") {
    auto result = truncate_output("", 100, "\n...[truncated]");
    REQUIRE(result.empty());
}

TEST_CASE("ToolOutputConfig default values", "[tool_output]") {
    ToolOutputConfig config;
    REQUIRE(config.max_bytes == 65536);
    REQUIRE(config.truncate_marker == "\n...[truncated]");
    REQUIRE(config.per_tool.empty());
}

TEST_CASE("ToolOutputConfig get_limit for specific tool", "[tool_output]") {
    ToolOutputConfig config;
    config.max_bytes = 65536;
    config.per_tool["shell"] = 32768;
    REQUIRE(config.get_limit("shell") == 32768);
    REQUIRE(config.get_limit("file") == 65536);
}
