// TraceEvent — structured trace event model (OTel/ECS style)
//
// Aligned with zeroclaw's LogEvent schema:
//   - @timestamp, severity_number/text, event.{category,action,outcome}
//   - service descriptor, trace_id/span_id/parent_span_id
//   - ea.* attribution namespace (parallels zeroclaw.*)
//   - attributes for free-form payload
#pragma once

#include "base/Types.h"
#include <string>

namespace ea::trace {

enum class Severity { Trace, Debug, Info, Warn, Error };

enum class EventCategory {
    Agent,     // agent turn lifecycle
    Tool,      // tool execution
    Provider,  // LLM call
    Memory,    // memory operations
    System     // startup/shutdown/config
};

enum class EventOutcome { Unknown, Success, Failure };

struct TraceEvent {
    std::string id;                          // UUID v4
    std::string timestamp;                   // ISO 8601 UTC with ms
    Severity severity = Severity::Info;
    EventCategory category = EventCategory::System;
    std::string action;                      // turn_start, tool_execute, etc.
    EventOutcome outcome = EventOutcome::Unknown;

    // OTel correlation
    std::string trace_id;                    // groups events from one agent run
    std::string span_id;                     // sub-operation identifier
    std::string parent_span_id;              // nesting (e.g. subagent)

    // ea.* attribution namespace — aligned with zeroclaw.* model
    // Current fields: agent_id, tool, model
    // Future: channel, channel_type, channel_alias, cron_job_id, session_key, etc.
    json ea;

    std::string message;                     // human-readable short message
    json attributes;                         // free-form structured payload

    int schema_version = 2;

    // Serialization
    json to_json() const;
    static TraceEvent from_json(const json& j);
};

// --- Enum helpers ---

const char* severity_text(Severity s);
int severity_number(Severity s);
const char* category_str(EventCategory c);
const char* outcome_str(EventOutcome o);
EventOutcome parse_outcome(const std::string& s);

// --- UUID generation ---

std::string generate_uuid();

// --- Timestamp generation ---

std::string generate_timestamp();

}  // namespace ea::trace
