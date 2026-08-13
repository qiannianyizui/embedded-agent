#include "Logger.h"
#include "RotatingSink.h"
#include "config/Config.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace ea::log {

static std::shared_ptr<spdlog::logger> g_logger;

Config Config::from(const ea::config::LogConfig& cfg, const std::string& log_dir) {
    Config c;
    c.log_dir = log_dir;
    c.persist = cfg.persist;
    c.verbose = cfg.verbose;
    c.max_file_bytes = cfg.max_file_bytes;
    c.max_total_bytes = cfg.max_total_bytes;

    // Parse level string
    auto level = spdlog::level::from_str(cfg.level);
    // spdlog::level::from_str returns off for unknown strings; fall back to debug
    if (level == spdlog::level::off && cfg.level != "off") {
        level = spdlog::level::debug;
    }
    c.level = level;
    return c;
}

void init(const Config& cfg) {
    std::vector<spdlog::sink_ptr> sinks;

    // Console sink — independent level: off unless verbose
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_level(cfg.verbose ? cfg.level : spdlog::level::off);
    sinks.push_back(console);

    // Rotating file sink
    if (cfg.persist) {
        auto file = std::make_shared<RotatingSink>(
            cfg.log_dir, cfg.max_file_bytes, cfg.max_total_bytes);
        file->set_level(cfg.level);
        sinks.push_back(file);
    }

    g_logger = std::make_shared<spdlog::logger>("ea", sinks.begin(), sinks.end());
    g_logger->set_level(cfg.level);
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    g_logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(g_logger);
}

std::shared_ptr<spdlog::logger> get() {
    if (!g_logger) {
        Config fallback;
        fallback.log_dir = "/tmp/.embedded-agent/logs";
        init(fallback);
    }
    return g_logger;
}

}  // namespace ea::log
