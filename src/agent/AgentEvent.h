// AgentEvent — lifecycle event types for the agent loop
#pragma once
#include "common/base/Types.h"
#include "budget/Types.h"
#include <string>

namespace ea::agent {

enum class AgentEventType {
    TurnStart,       // Iteration begins
    TurnEnd,         // Iteration ends (should_stop)
    ToolCallStart,   // Tool execution begins
    ToolCallEnd,     // Tool execution completes (with result)
    LLMResponse,     // LLM returns a response
    Error,           // An error occurred
    Interrupt        // Agent was interrupted
};

struct AgentEvent {
    AgentEventType type;
    int iteration = 0;
    std::string agent_id;           // Empty for main agent, set for subagents

    // Event data — different fields used per event type
    std::string user_input;         // TurnStart
    std::string assistant_output;   // TurnEnd, LLMResponse
    std::string tool_name;          // ToolCallStart, ToolCallEnd
    json tool_arguments;            // ToolCallStart
    std::string tool_result;        // ToolCallEnd
    bool tool_error = false;        // ToolCallEnd
    std::string error_message;      // Error
    Usage usage;                    // LLMResponse (token usage)
    budget::UsageSnapshot turn_usage;     // TurnEnd
    budget::CostSnapshot turn_cost;       // TurnEnd
};

}  // namespace ea::agent
