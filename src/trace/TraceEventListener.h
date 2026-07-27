// TraceEventListener — bridges AgentEvent → TraceEvent and writes to JsonlWriter.
//
// Converts the agent loop's event stream into structured trace events,
// applying I/O capture policies (tool_io, llm_payload).
#pragma once

#include "agent/IEventListener.h"
#include "JsonlWriter.h"
#include <memory>
#include <string>

namespace ea::trace {

enum class ToolIoPolicy { Off, Redacted, Full };
enum class LlmPayloadPolicy { Off, Redacted, Full };

class TraceEventListener : public agent::IEventListener {
public:
    TraceEventListener(std::shared_ptr<JsonlWriter> writer,
                       ToolIoPolicy tool_io,
                       int tool_io_truncate_bytes,
                       LlmPayloadPolicy llm_payload)
        : writer_(std::move(writer))
        , tool_io_(tool_io)
        , tool_io_truncate_bytes_(tool_io_truncate_bytes)
        , llm_payload_(llm_payload)
    {}

    void on_event(const agent::AgentEvent& event) override;

    void set_trace_id(const std::string& trace_id) { trace_id_ = trace_id; }

private:
    std::shared_ptr<JsonlWriter> writer_;
    ToolIoPolicy tool_io_;
    int tool_io_truncate_bytes_;
    LlmPayloadPolicy llm_payload_;
    std::string trace_id_;
    std::string model_;  // last known model from LLMResponse

    // I/O capture helpers
    std::string capture_tool_io(const std::string& data) const;
    std::string capture_llm_payload(const std::string& data) const;

    // Build a TraceEvent with common fields filled
    TraceEvent make_event(Severity severity,
                          EventCategory category,
                          const std::string& action,
                          const agent::AgentEvent& src) const;
};

// --- Policy parsing ---

inline ToolIoPolicy parse_tool_io_policy(const std::string& s) {
    if (s == "full" || s == "on" || s == "yes" || s == "true") return ToolIoPolicy::Full;
    if (s == "off" || s == "no" || s == "false") return ToolIoPolicy::Off;
    return ToolIoPolicy::Redacted;
}

inline LlmPayloadPolicy parse_llm_payload_policy(const std::string& s) {
    if (s == "redacted") return LlmPayloadPolicy::Redacted;
    if (s == "full" || s == "on" || s == "yes" || s == "true") return LlmPayloadPolicy::Full;
    return LlmPayloadPolicy::Off;
}

}  // namespace ea::trace
