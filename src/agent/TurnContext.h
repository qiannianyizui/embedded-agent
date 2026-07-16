// TurnContext — per-iteration state for the agent loop step chain
#pragma once
#include "core/Types.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include <vector>
#include <string>
#include <atomic>

namespace ea::agent {

struct TurnContext {
    // Non-copyable — holds reference to atomic and message history
    TurnContext(const TurnContext&) = delete;
    TurnContext& operator=(const TurnContext&) = delete;
    TurnContext(TurnContext&&) = default;
    TurnContext& operator=(TurnContext&&) = default;

    // Message history (shared with AgentLoop)
    std::vector<Message>& messages;

    // Per-iteration state
    std::vector<ToolCall> pending_tool_calls;
    std::vector<ToolResult> tool_results;
    std::vector<ToolSpec> tool_specs;
    LLMResponse response;

    // Iteration tracking
    int iteration = 0;
    int max_iterations = 90;
    int max_tool_output_bytes = 65536;

    // Control flags
    bool should_stop = false;
    std::atomic<bool>& interrupted;

    // Dependencies (non-owning)
    IProvider* provider = nullptr;
    tool::ToolRegistry* registry = nullptr;

    // System prompt (built once, reused)
    std::string system_prompt;

    TurnContext(std::vector<Message>& msgs, std::atomic<bool>& intr)
        : messages(msgs), interrupted(intr) {}
};

}  // namespace ea::agent
