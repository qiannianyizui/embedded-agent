#include <catch2/catch_test_macros.hpp>
#include "agent/SystemPrompt.h"
#include "base/Types.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("SystemPrompt with soul only", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.soul = "You are a helpful assistant";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Identity") != std::string::npos);
    REQUIRE(prompt.find("You are a helpful assistant") != std::string::npos);
}

TEST_CASE("SystemPrompt with tool guidance", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.tool_guidance = "Use shell tool for commands";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Tool Guidance") != std::string::npos);
    REQUIRE(prompt.find("Use shell tool for commands") != std::string::npos);
}

TEST_CASE("SystemPrompt with platform info", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.platform_info = "Linux x86_64";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Environment") != std::string::npos);
    REQUIRE(prompt.find("Linux x86_64") != std::string::npos);
}

TEST_CASE("SystemPrompt includes skills index", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.skills_index =
        "# Skills (mandatory)\n"
        "<available_skills>\n"
        "  development:\n"
        "    - cpp-conventions: C++ conventions\n"
        "</available_skills>";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("<available_skills>") != std::string::npos);
    REQUIRE(prompt.find("cpp-conventions") != std::string::npos);
}

TEST_CASE("SystemPrompt with relevant memories", "[agent][systemprompt]") {
    PromptContext ctx;
    MemoryEntry mem;
    mem.content = "User prefers dark mode";
    mem.category = "preference";
    mem.importance = 7;
    ctx.relevant_memories.push_back(mem);

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Relevant Memories") != std::string::npos);
    REQUIRE(prompt.find("[preference] User prefers dark mode") != std::string::npos);
}

TEST_CASE("SystemPrompt with user profile", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.user_profile = "Senior developer, prefers C++";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# User Profile") != std::string::npos);
    REQUIRE(prompt.find("Senior developer, prefers C++") != std::string::npos);
}

TEST_CASE("SystemPrompt with curated memory", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.curated_memory = "- Project uses CMake\n- Prefer TDD";

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Persistent Memory") != std::string::npos);
    REQUIRE(prompt.find("Project uses CMake") != std::string::npos);
    REQUIRE(prompt.find("Prefer TDD") != std::string::npos);
}

TEST_CASE("SystemPrompt always includes timestamp", "[agent][systemprompt]") {
    PromptContext ctx;  // all empty

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("Current time:") != std::string::npos);
}

TEST_CASE("SystemPrompt empty context produces minimal prompt", "[agent][systemprompt]") {
    PromptContext ctx;

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("# Identity") != std::string::npos);
    // Should NOT have sections for empty fields
    REQUIRE(prompt.find("# Tool Guidance") == std::string::npos);
    REQUIRE(prompt.find("# Environment") == std::string::npos);
    REQUIRE(prompt.find("# Relevant Memories") == std::string::npos);
    REQUIRE(prompt.find("# User Profile") == std::string::npos);
}

TEST_CASE("SystemPrompt full context", "[agent][systemprompt]") {
    PromptContext ctx;
    ctx.soul = "I am an embedded agent";
    ctx.tool_guidance = "Available tools: shell, file, search";
    ctx.platform_info = "Linux ARM64";
    ctx.user_profile = "Embedded systems engineer";

    MemoryEntry mem;
    mem.content = "Project uses CMake";
    mem.category = "project";
    ctx.relevant_memories.push_back(mem);

    auto prompt = build_system_prompt(ctx);

    REQUIRE(prompt.find("I am an embedded agent") != std::string::npos);
    REQUIRE(prompt.find("Available tools: shell, file, search") != std::string::npos);
    REQUIRE(prompt.find("Linux ARM64") != std::string::npos);
    REQUIRE(prompt.find("[project] Project uses CMake") != std::string::npos);
    REQUIRE(prompt.find("Embedded systems engineer") != std::string::npos);
    REQUIRE(prompt.find("Current time:") != std::string::npos);
}
