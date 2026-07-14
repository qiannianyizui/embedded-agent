#include "Logger.h"

namespace ea::log {

static std::shared_ptr<spdlog::logger> g_logger;

void init(const std::string& log_dir, bool debug) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        log_dir + "/agent.log", true);

    std::vector<spdlog::sink_ptr> sinks{console, file};
    g_logger = std::make_shared<spdlog::logger>("ea", sinks.begin(), sinks.end());

    if (debug) {
        g_logger->set_level(spdlog::level::debug);
    } else {
        g_logger->set_level(spdlog::level::info);
    }
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(g_logger);
}

std::shared_ptr<spdlog::logger> get() {
    if (!g_logger) {
        init("/tmp", false);
    }
    return g_logger;
}

}  // namespace ea::log
