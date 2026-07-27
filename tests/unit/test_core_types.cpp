#include <catch2/catch_test_macros.hpp>
#include "base/Types.h"

using namespace ea;

TEST_CASE("LLMResponse is_tool_use with tool_use stop reason", "[types]") {
    LLMResponse resp;
    resp.stop_reason = "tool_use";
    REQUIRE(resp.is_tool_use());
}

TEST_CASE("LLMResponse is_tool_use with tool_calls stop reason", "[types]") {
    LLMResponse resp;
    resp.stop_reason = "tool_calls";
    REQUIRE(resp.is_tool_use());
}

TEST_CASE("LLMResponse is_tool_use false for stop", "[types]") {
    LLMResponse resp;
    resp.stop_reason = "stop";
    REQUIRE_FALSE(resp.is_tool_use());
}

TEST_CASE("LLMResponse is_tool_use false for end_turn", "[types]") {
    LLMResponse resp;
    resp.stop_reason = "end_turn";
    REQUIRE_FALSE(resp.is_tool_use());
}

TEST_CASE("LLMResponse is_tool_use false for empty", "[types]") {
    LLMResponse resp;
    REQUIRE_FALSE(resp.is_tool_use());
}

TEST_CASE("ToolResult default values", "[types]") {
    ToolResult result;
    REQUIRE(result.call_id == "");
    REQUIRE(result.output == "");
    REQUIRE(result.is_error == false);
}

TEST_CASE("Usage default values", "[types]") {
    Usage usage;
    REQUIRE(usage.input_tokens == 0);
    REQUIRE(usage.output_tokens == 0);
    REQUIRE(usage.cache_read_tokens == 0);
    REQUIRE(usage.cache_write_tokens == 0);
}

TEST_CASE("ChatOptions default values", "[types]") {
    ChatOptions opts;
    REQUIRE(opts.temperature == 0.7f);
    REQUIRE(opts.max_tokens == 4096);
    REQUIRE(opts.top_p == 1);
    REQUIRE(opts.stream == false);
    REQUIRE_FALSE(opts.stop.has_value());
}

TEST_CASE("MemoryEntry default values", "[types]") {
    MemoryEntry entry;
    REQUIRE(entry.importance == 5);
    REQUIRE_FALSE(entry.agent_id.has_value());
}
