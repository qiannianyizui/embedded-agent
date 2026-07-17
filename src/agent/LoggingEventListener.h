// LoggingEventListener — built-in listener that logs agent lifecycle events
#pragma once
#include "IEventListener.h"
#include "common/io/Logger.h"

namespace ea::agent {

class LoggingEventListener : public IEventListener {
public:
    void on_event(const AgentEvent& event) override {
        std::string aid = event.agent_id.empty() ? "" : " " + event.agent_id;
        switch (event.type) {
        case AgentEventType::TurnStart:
            EA_INFO("[agent{}] Turn {} start: {}", aid, event.iteration,
                    event.user_input.substr(0, 80));
            break;
        case AgentEventType::ToolCallStart:
            EA_INFO("[agent{}] Tool call: {}", aid, event.tool_name);
            break;
        case AgentEventType::ToolCallEnd:
            EA_DEBUG("[agent{}] Tool result: {} ({} bytes, error={})",
                     aid, event.tool_name, event.tool_result.size(), event.tool_error);
            break;
        case AgentEventType::Error:
            EA_ERROR("[agent{}] Error: {}", aid, event.error_message);
            break;
        case AgentEventType::Interrupt:
            EA_WARN("[agent{}] Interrupted at iteration {}", aid, event.iteration);
            break;
        default:
            break;
        }
    }
};

}  // namespace ea::agent
