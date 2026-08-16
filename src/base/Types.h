#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include "nlohmann/json.hpp"

namespace ea {

using json = nlohmann::json;

enum class Role { System, User, Assistant, Tool };

struct ToolCall {
    std::string id;
    std::string name;
    json arguments;
};

struct Message {
    Message() = default;
    Message(Role role,
            std::string content,
            std::optional<std::string> name = std::nullopt,
            std::optional<std::vector<ToolCall>> tool_calls = std::nullopt,
            std::optional<std::string> tool_call_id = std::nullopt,
            std::string mode = {},
            std::string plan_file = {})
        : role(role)
        , content(std::move(content))
        , name(std::move(name))
        , tool_calls(std::move(tool_calls))
        , tool_call_id(std::move(tool_call_id))
        , mode(std::move(mode))
        , plan_file(std::move(plan_file)) {}

    Role role;
    std::string content;
    std::optional<std::string> name;
    std::optional<std::vector<ToolCall>> tool_calls;
    std::optional<std::string> tool_call_id;
    std::string mode;       // "plan" / "build" (empty for legacy messages)
    std::string plan_file;  // plan file active when this message was sent
};

struct Usage {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;
};

struct LLMResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    Usage usage;
    std::string stop_reason;
    bool is_tool_use() const {
        return stop_reason == "tool_use" || stop_reason == "tool_calls";
    }
};

struct StreamChunk {
    enum class Type { Content, ToolCallBegin, ToolCallDelta, ToolCallEnd, Done, Error };
    Type type;
    std::string data;
    std::optional<ToolCall> tool_call;
    std::optional<Usage> usage;
    std::optional<std::string> finish_reason;
};

struct ToolSpec {
    std::string name;
    std::string description;
    json parameters;
};

struct ToolResult {
    std::string call_id;
    std::string output;
    bool is_error = false;
};

struct MemoryEntry {
    std::string id;
    std::string content;
    std::string category;
    int importance = 5;
    std::string created_at;
    std::optional<std::string> agent_id;
};

struct ChatOptions {
    float temperature = 0.7f;
    int max_tokens = 0;  // 0 = don't send; use the model's default
    std::optional<std::string> stop;
    float top_p = 1.0f;
    bool stream = false;
    std::optional<std::string> route_hint;  // "code", "fast", "vision", "cheap"
};

}  // namespace ea
