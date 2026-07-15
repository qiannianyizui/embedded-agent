#include <catch2/catch_test_macros.hpp>
#include "provider/PromptGuidedTools.h"
#include "core/Types.h"

using namespace ea;
using namespace ea::provider;

TEST_CASE("PromptGuidedTools injects tools into prompt", "[prompt_guided]") {
    std::string system = "You are helpful.";
    std::vector<ToolSpec> tools = {
        ToolSpec{"get_weather", "Get weather for a city",
                 json::parse(R"({"type":"object","properties":{"city":{"type":"string"}}})")}
    };

    auto result = inject_tools_into_prompt(system, tools);

    REQUIRE(result.find("You are helpful.") != std::string::npos);
    REQUIRE(result.find("get_weather") != std::string::npos);
    REQUIRE(result.find("Get weather for a city") != std::string::npos);
    REQUIRE(result.find("city") != std::string::npos);
}

TEST_CASE("PromptGuidedTools returns original prompt when no tools", "[prompt_guided]") {
    std::string system = "You are helpful.";
    auto result = inject_tools_into_prompt(system, {});
    REQUIRE(result == "You are helpful.");
}

TEST_CASE("PromptGuidedTools injects multiple tools", "[prompt_guided]") {
    std::string system = "You are helpful.";
    std::vector<ToolSpec> tools = {
        ToolSpec{"tool_a", "Description A", json::parse(R"({"type":"object"})")},
        ToolSpec{"tool_b", "Description B", json::parse(R"({"type":"object"})")}
    };

    auto result = inject_tools_into_prompt(system, tools);

    REQUIRE(result.find("tool_a") != std::string::npos);
    REQUIRE(result.find("tool_b") != std::string::npos);
    REQUIRE(result.find("Description A") != std::string::npos);
    REQUIRE(result.find("Description B") != std::string::npos);
}
