#include "TraceEventListener.h"
#include "Scrub.h"
#include "budget/Types.h"

namespace ea::trace {

void TraceEventListener::on_event(const agent::AgentEvent& event) {
    if (!writer_) return;

    // Track trace_id from the AgentEvent (set by AgentLoop::run)
    if (!event.trace_id.empty()) {
        trace_id_ = event.trace_id;
    }

    switch (event.type) {
    case agent::AgentEventType::TurnStart: {
        TraceEvent te = make_event(Severity::Info, EventCategory::Agent,
                                   "turn_start", event);
        te.message = "Turn started";
        if (llm_payload_ != LlmPayloadPolicy::Off) {
            std::string scrubbed = scrub_credentials(event.user_input);
            te.attributes["user_input"] = capture_llm_payload(scrubbed);
        } else {
            te.attributes["user_input_bytes"] = event.user_input.size();
        }
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::TurnEnd: {
        TraceEvent te = make_event(Severity::Info, EventCategory::Agent,
                                   "turn_end", event);
        te.message = "Turn ended";
        te.attributes["output_bytes"] = event.assistant_output.size();
        // Usage/cost snapshot
        const auto& u = event.turn_usage;
        json usage_obj;
        usage_obj["input_tokens"] = u.input_tokens;
        usage_obj["output_tokens"] = u.output_tokens;
        usage_obj["cache_read_tokens"] = u.cache_read_tokens;
        usage_obj["cache_write_tokens"] = u.cache_write_tokens;
        te.attributes["turn_usage"] = usage_obj;
        const auto& c = event.turn_cost;
        json cost_obj;
        cost_obj["input_cost"] = c.input_cost;
        cost_obj["output_cost"] = c.output_cost;
        cost_obj["total_cost"] = c.total();
        te.attributes["turn_cost"] = cost_obj;
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::LLMRequest: {
        TraceEvent te = make_event(Severity::Info, EventCategory::Provider,
                                   "llm_request", event);
        te.message = "LLM request sent";
        te.ea["agent_id"] = event.agent_id;
        te.attributes["messages_count"] = event.messages_count;
        te.attributes["iteration"] = event.iteration;
        // Model attribution
        if (!event.model.empty()) {
            te.ea["model"] = event.model;
            model_ = event.model;  // Remember for LLMResponse
        }
        // Request payload: opt-in via llm_payload policy (default off)
        if (llm_payload_ != LlmPayloadPolicy::Off && !event.request_messages.empty()) {
            std::string scrubbed = scrub_credentials(event.request_messages);
            te.attributes["request_messages"] = capture_llm_payload(scrubbed);
            if (static_cast<int>(scrubbed.size()) > tool_io_truncate_bytes_) {
                te.attributes["request_messages_truncated"] = true;
                te.attributes["request_messages_original_bytes"] = static_cast<int>(scrubbed.size());
            }
        }
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::ToolCallStart: {
        TraceEvent te = make_event(Severity::Info, EventCategory::Tool,
                                   "tool_execute", event);
        te.message = "Tool execution started: " + event.tool_name;
        te.ea["tool"] = event.tool_name;
        te.ea["agent_id"] = event.agent_id;
        if (tool_io_ != ToolIoPolicy::Off) {
            te.attributes["tool_arguments"] = capture_tool_io(event.tool_arguments.dump());
        }
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::ToolCallEnd: {
        TraceEvent te = make_event(
            event.tool_error ? Severity::Warn : Severity::Info,
            EventCategory::Tool, "tool_execute", event);
        te.outcome = event.tool_error ? EventOutcome::Failure : EventOutcome::Success;
        te.message = "Tool execution completed: " + event.tool_name;
        te.ea["tool"] = event.tool_name;
        te.ea["agent_id"] = event.agent_id;
        te.attributes["tool_result_bytes"] = event.tool_result.size();
        te.attributes["error"] = event.tool_error;
        if (tool_io_ != ToolIoPolicy::Off) {
            te.attributes["tool_result"] = capture_tool_io(event.tool_result);
        }
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::LLMResponse: {
        TraceEvent te = make_event(Severity::Info, EventCategory::Provider,
                                   "llm_response", event);
        te.message = "LLM response received";
        // Usage
        json usage_obj;
        usage_obj["input_tokens"] = event.usage.input_tokens;
        usage_obj["output_tokens"] = event.usage.output_tokens;
        te.attributes["usage"] = usage_obj;
        te.attributes["output_bytes"] = event.assistant_output.size();
        te.attributes["tool_calls_count"] = event.tool_calls_count;
        // Always record raw_response (after scrubbing) — core observability data
        if (!event.assistant_output.empty()) {
            te.attributes["raw_response"] = scrub_credentials(
                capture_llm_payload(event.assistant_output));
        }
        // Model attribution
        te.ea["agent_id"] = event.agent_id;
        if (!event.model.empty()) {
            te.ea["model"] = event.model;
            model_ = event.model;
        } else if (!model_.empty()) {
            te.ea["model"] = model_;
        }
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::Error: {
        TraceEvent te = make_event(Severity::Error, EventCategory::Agent,
                                   "turn_end", event);
        te.outcome = EventOutcome::Failure;
        te.message = "Agent error";
        te.ea["agent_id"] = event.agent_id;
        te.attributes["error_message"] = event.error_message;
        writer_->write(te);
        break;
    }
    case agent::AgentEventType::Interrupt: {
        TraceEvent te = make_event(Severity::Warn, EventCategory::Agent,
                                   "turn_end", event);
        te.message = "Agent interrupted";
        te.ea["agent_id"] = event.agent_id;
        te.attributes["interrupted"] = true;
        writer_->write(te);
        break;
    }
    }
}

std::string TraceEventListener::capture_tool_io(const std::string& data) const {
    if (tool_io_ == ToolIoPolicy::Full) return data;
    // Redacted: truncate on char boundary
    if (static_cast<int>(data.size()) <= tool_io_truncate_bytes_) return data;
    // Find a safe truncation point on a UTF-8 char boundary
    int end = tool_io_truncate_bytes_;
    while (end > 0 && (static_cast<unsigned char>(data[end]) & 0xC0) == 0x80) {
        --end;
    }
    return data.substr(0, end);
}

std::string TraceEventListener::capture_llm_payload(const std::string& data) const {
    if (llm_payload_ == LlmPayloadPolicy::Full) return data;
    if (static_cast<int>(data.size()) <= tool_io_truncate_bytes_) return data;
    int end = tool_io_truncate_bytes_;
    while (end > 0 && (static_cast<unsigned char>(data[end]) & 0xC0) == 0x80) {
        --end;
    }
    return data.substr(0, end);
}

TraceEvent TraceEventListener::make_event(Severity severity,
                                          EventCategory category,
                                          const std::string& action,
                                          const agent::AgentEvent& src) const {
    TraceEvent te;
    te.id = generate_uuid();
    te.timestamp = generate_timestamp();
    te.severity = severity;
    te.category = category;
    te.action = action;
    te.trace_id = trace_id_;
    te.ea = json::object();
    te.ea["agent_id"] = src.agent_id;
    return te;
}

}  // namespace ea::trace
