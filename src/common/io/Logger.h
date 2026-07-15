#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace ea::log {

void init(const std::string& log_dir, bool debug = false);
std::shared_ptr<spdlog::logger> get();

}  // namespace ea::log

#define EA_DEBUG(...)    ea::log::get()->debug(__VA_ARGS__)
#define EA_INFO(...)     ea::log::get()->info(__VA_ARGS__)
#define EA_WARN(...)     ea::log::get()->warn(__VA_ARGS__)
#define EA_ERROR(...)    ea::log::get()->error(__VA_ARGS__)
