// Logger — spdlog-based logging with per-sink level control
#pragma once
#include <spdlog/spdlog.h>
#include <memory>
#include <string>

namespace ea::config { struct LogConfig; }  // forward

namespace ea::log {

/// Runtime logging configuration, constructed from config::LogConfig + log_dir.
struct Config {
    std::string log_dir;
    spdlog::level::level_enum level = spdlog::level::debug;
    bool persist = true;
    bool verbose = false;
    int max_file_bytes = 10 * 1024 * 1024;
    int max_total_bytes = 100 * 1024 * 1024;

    /// Build from config::LogConfig + resolved log directory.
    static Config from(const ea::config::LogConfig& cfg, const std::string& log_dir);
};

/// Initialize the global logger. Can be called more than once (replaces the logger).
void init(const Config& cfg);

/// Get the global logger instance. Auto-initializes with defaults if init() was never called.
std::shared_ptr<spdlog::logger> get();

}  // namespace ea::log

#define EA_DEBUG(...)    ea::log::get()->debug(__VA_ARGS__)
#define EA_INFO(...)     ea::log::get()->info(__VA_ARGS__)
#define EA_WARN(...)     ea::log::get()->warn(__VA_ARGS__)
#define EA_ERROR(...)    ea::log::get()->error(__VA_ARGS__)
