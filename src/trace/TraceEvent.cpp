#include "TraceEvent.h"
#include "embedded-agent/Version.h"
#include <uuid/uuid.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace ea::trace {

// --- Enum helpers ---

const char* severity_text(Severity s) {
    switch (s) {
    case Severity::Trace: return "TRACE";
    case Severity::Debug: return "DEBUG";
    case Severity::Info:  return "INFO";
    case Severity::Warn:  return "WARN";
    case Severity::Error: return "ERROR";
    }
    return "INFO";
}

int severity_number(Severity s) {
    switch (s) {
    case Severity::Trace: return 1;
    case Severity::Debug: return 5;
    case Severity::Info:  return 9;
    case Severity::Warn:  return 13;
    case Severity::Error: return 17;
    }
    return 9;
}

const char* category_str(EventCategory c) {
    switch (c) {
    case EventCategory::Agent:    return "agent";
    case EventCategory::Tool:     return "tool";
    case EventCategory::Provider: return "provider";
    case EventCategory::Memory:   return "memory";
    case EventCategory::System:   return "system";
    }
    return "system";
}

const char* outcome_str(EventOutcome o) {
    switch (o) {
    case EventOutcome::Unknown:  return "unknown";
    case EventOutcome::Success:  return "success";
    case EventOutcome::Failure:  return "failure";
    }
    return "unknown";
}

EventOutcome parse_outcome(const std::string& s) {
    if (s == "success") return EventOutcome::Success;
    if (s == "failure") return EventOutcome::Failure;
    return EventOutcome::Unknown;
}

// --- UUID generation ---

std::string generate_uuid() {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char buf[37];
    uuid_unparse_lower(uuid, buf);
    return std::string(buf, 36);
}

// --- Timestamp generation (ISO 8601 UTC with milliseconds) ---

std::string generate_timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto time_t_now = system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&time_t_now, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

// --- Serialization ---

json TraceEvent::to_json() const {
    json j;
    j["id"] = id;
    j["@timestamp"] = timestamp;
    j["severity_number"] = severity_number(severity);
    j["severity_text"] = severity_text(severity);

    json event_obj;
    event_obj["category"] = category_str(category);
    event_obj["action"] = action;
    if (outcome != EventOutcome::Unknown) {
        event_obj["outcome"] = outcome_str(outcome);
    }
    j["event"] = event_obj;

    // Service descriptor — constant, aligned with OTel
    json service_obj;
    service_obj["name"] = "embedded-agent";
    service_obj["version"] = EMBEDDED_AGENT_VERSION;
    j["service"] = service_obj;

    // OTel correlation
    j["trace_id"] = trace_id;
    j["span_id"] = span_id;
    if (!parent_span_id.empty()) {
        j["parent_span_id"] = parent_span_id;
    }

    // ea.* attribution namespace
    if (ea.is_object() && !ea.empty()) {
        j["ea"] = ea;
    } else {
        j["ea"] = json::object();
    }

    j["message"] = message;

    // Attributes — omit if null
    if (!attributes.is_null()) {
        j["attributes"] = attributes;
    } else {
        j["attributes"] = json::object();
    }

    j["schema_version"] = schema_version;
    return j;
}

TraceEvent TraceEvent::from_json(const json& j) {
    TraceEvent e;
    e.id = j.value("id", "");
    e.timestamp = j.value("@timestamp", "");

    // Severity from number
    int sn = j.value("severity_number", 9);
    if (sn <= 4) e.severity = Severity::Trace;
    else if (sn <= 8) e.severity = Severity::Debug;
    else if (sn <= 12) e.severity = Severity::Info;
    else if (sn <= 16) e.severity = Severity::Warn;
    else e.severity = Severity::Error;

    // Category
    std::string cat = j.contains("event") && j["event"].contains("category")
        ? j["event"]["category"].get<std::string>() : "system";
    if (cat == "agent") e.category = EventCategory::Agent;
    else if (cat == "tool") e.category = EventCategory::Tool;
    else if (cat == "provider") e.category = EventCategory::Provider;
    else if (cat == "memory") e.category = EventCategory::Memory;
    else e.category = EventCategory::System;

    e.action = j.contains("event") && j["event"].contains("action")
        ? j["event"]["action"].get<std::string>() : "";

    std::string out = j.contains("event") && j["event"].contains("outcome")
        ? j["event"]["outcome"].get<std::string>() : "unknown";
    e.outcome = parse_outcome(out);

    e.trace_id = j.value("trace_id", "");
    e.span_id = j.value("span_id", "");
    e.parent_span_id = j.value("parent_span_id", "");
    e.ea = j.value("ea", json::object());
    e.message = j.value("message", "");
    e.attributes = j.value("attributes", json::object());
    e.schema_version = j.value("schema_version", 2);
    return e;
}

}  // namespace ea::trace
