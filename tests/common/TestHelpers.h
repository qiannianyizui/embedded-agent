// tests/common/TestHelpers.h
#pragma once
#include "core/Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ea::test {

using json = nlohmann::json;

inline ToolCall make_call(const std::string& tool, const std::string& id,
                          const json& args = json::object()) {
    ToolCall tc;
    tc.id = id;
    tc.name = tool;
    tc.arguments = args;
    return tc;
}

inline LLMResponse make_text(const std::string& text, const std::string& stop = "stop") {
    LLMResponse r;
    r.content = text;
    r.stop_reason = stop;
    return r;
}

inline LLMResponse make_tools(std::vector<ToolCall> calls) {
    LLMResponse r;
    r.content = "";
    r.stop_reason = "tool_use";
    r.tool_calls = std::move(calls);
    return r;
}

inline LLMResponse make_error_response(const std::string& /*error_msg*/) {
    LLMResponse r;
    r.content = "";
    r.stop_reason = "error";
    return r;
}

}  // namespace ea::test
