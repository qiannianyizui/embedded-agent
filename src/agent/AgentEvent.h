// AgentEvent — lifecycle event types for the agent loop
#pragma once
#include "base/Types.h"
#include "budget/Types.h"
#include <string>

namespace ea::agent {

enum class AgentEventType {
    TurnStart,       // Iteration begins
    TurnEnd,         // Iteration ends (should_stop)
    LLMRequest,      // LLM provider call begins
    ToolCallStart,   // Tool execution begins
    ToolCallEnd,     // Tool execution completes (with result)
    LLMResponse,     // LLM returns a response
    ModeChanged,     // Permission mode switched (plan_exit handoff)
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

    // LLM interaction data
    std::string request_messages;   // LLMRequest (serialized JSON [{role,content},...])
    int messages_count = 0;         // LLMRequest
    std::string model;              // LLMRequest, LLMResponse
    int tool_calls_count = 0;       // LLMResponse
    std::string mode;               // ModeChanged (permission mode name)

    // Trace correlation — groups events from one agent run()
    std::string trace_id;
};

}  // namespace ea::agent
