// Trace — public interface for the trace subsystem
#pragma once

#include "JsonlWriter.h"
#include "TraceEvent.h"
#include "TraceEventListener.h"
#include "config/Config.h"
#include <memory>
#include <string>

namespace ea::trace {

struct Config {
    std::string trace_dir;
    StoragePolicy persist = StoragePolicy::Rolling;  // none | rolling | full
    int max_entries = 10000;
    ToolIoPolicy tool_io = ToolIoPolicy::Redacted;
    int tool_io_truncate_bytes = 40960;
    LlmPayloadPolicy llm_payload = LlmPayloadPolicy::Off;

    static Config from(const ea::config::TraceConfig& cfg, const std::string& trace_dir);
};

void init(const Config& cfg);
void shutdown();
std::shared_ptr<JsonlWriter> writer();

// Create a TraceEventListener pre-configured from the current trace state
std::shared_ptr<TraceEventListener> create_listener();

}  // namespace ea::trace
