// LoggingEventListener — logs only problem-level agent events.
// Normal operation (turn start, tool call names, tool result sizes) is trace
// data and does NOT belong in the log system — it belongs in a future trace
// channel. Here we record only failures and interrupts.
#pragma once
#include "IEventListener.h"
#include "log/Logger.h"

namespace ea::agent {

class LoggingEventListener : public IEventListener {
public:
    void on_event(const AgentEvent& event) override {
        std::string aid = event.agent_id.empty() ? "" : " " + event.agent_id;
        switch (event.type) {
        case AgentEventType::ToolCallEnd:
            if (event.tool_error) {
                EA_WARN("[agent{}] Tool {} failed", aid, event.tool_name);
            }
            break;
        case AgentEventType::Error:
            EA_ERROR("[agent{}] Error: {}", aid, event.error_message);
            break;
        case AgentEventType::Interrupt:
            EA_WARN("[agent{}] Interrupted at iteration {}", aid, event.iteration);
            break;
        default:
            // TurnStart, TurnEnd, LLMRequest, ToolCallStart, LLMResponse — trace data, not logged
            break;
        }
    }
};

}  // namespace ea::agent
