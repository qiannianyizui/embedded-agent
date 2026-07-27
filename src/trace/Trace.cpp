#include "Trace.h"
#include "log/Logger.h"

namespace ea::trace {

static std::shared_ptr<JsonlWriter> g_writer;
static Config g_config;

Config Config::from(const ea::config::TraceConfig& cfg, const std::string& trace_dir) {
    Config c;
    c.trace_dir = trace_dir;
    c.max_entries = cfg.max_entries;
    c.tool_io_truncate_bytes = cfg.tool_io_truncate_bytes;

    // Parse persist policy
    std::string p = cfg.persist;
    // Trim + lowercase
    while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
    for (auto& ch : p) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    if (p == "none") c.persist = StoragePolicy::None;
    else if (p == "full") c.persist = StoragePolicy::Full;
    else c.persist = StoragePolicy::Rolling;

    c.tool_io = parse_tool_io_policy(cfg.tool_io);
    c.llm_payload = parse_llm_payload_policy(cfg.llm_payload);
    return c;
}

void init(const Config& cfg) {
    g_config = cfg;
    if (cfg.persist == StoragePolicy::None) {
        EA_INFO("trace: persistence disabled");
        g_writer = nullptr;
        return;
    }
    g_writer = std::make_shared<JsonlWriter>(cfg.trace_dir, cfg.persist, cfg.max_entries);
    EA_INFO("trace: initialized (persist={}, max_entries={}, path={})",
            cfg.persist == StoragePolicy::Rolling ? "rolling" : "full",
            cfg.max_entries, g_writer->file_path());
}

void shutdown() {
    if (g_writer) {
        g_writer->flush();
    }
    g_writer.reset();
}

std::shared_ptr<JsonlWriter> writer() {
    return g_writer;
}

std::shared_ptr<TraceEventListener> create_listener() {
    if (!g_writer) return nullptr;
    return std::make_shared<TraceEventListener>(
        g_writer, g_config.tool_io, g_config.tool_io_truncate_bytes, g_config.llm_payload);
}

}  // namespace ea::trace
