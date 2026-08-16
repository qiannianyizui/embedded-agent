#include "app/AppBuilder.h"
#include "app/IRunner.h"
#include "platform/Platform.h"
#include "io/FileSystem.h"
#include "log/Logger.h"
#include "config/Config.h"
#include "trace/Trace.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <unistd.h>  // getcwd

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent (TUI)"};

    std::string config_path;
    bool verbose = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("-v,--verbose", verbose, "Output logs to terminal");

    // Optional explicit TUI subcommand; no subcommand also defaults to TUI.
    app.add_subcommand("tui", "Run in TUI mode (FTXUI interface)");

    // Allow 0 or 1 subcommand (0 = default mode)
    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    // Workspace = startup directory (before anything could chdir)
    char cwd_buf[4096];
    std::string cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : "";

    // Initialize logging with defaults (before config is loaded)
    auto cfg_dir = ea::fs::config_dir();
    std::string log_dir = cfg_dir.ok() ? cfg_dir.value() + "/logs" : "/tmp/.embedded-agent/logs";
    ea::log::Config log_cfg;
    log_cfg.log_dir = log_dir;
    log_cfg.verbose = verbose;
    ea::log::init(log_cfg);
    EA_INFO("embedded-agent v0.1.0 starting (mode: tui)");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }

    // Re-initialize logging from config (CLI --verbose takes precedence)
    log_cfg = ea::log::Config::from(cfg_result.value().log,
        cfg_dir.ok() ? cfg_dir.value() + "/logs" : "/tmp/.embedded-agent/logs");
    if (verbose) log_cfg.verbose = true;
    ea::log::init(log_cfg);

    // Initialize trace subsystem
    auto trace_dir_result = ea::fs::trace_dir();
    std::string trace_dir = trace_dir_result.ok() ? trace_dir_result.value() : "/tmp/.embedded-agent/trace";
    auto trace_cfg = ea::trace::Config::from(cfg_result.value().trace, trace_dir);
    ea::trace::init(trace_cfg);

    // Build application context (verbose flag also enables debug mode for agent)
    auto ctx_result = ea::app::AppBuilder::build(cfg_result.value(), verbose, cwd);
    if (!ctx_result.ok()) {
        EA_ERROR("App init failed: {}", ctx_result.error().message);
        std::cerr << "Init error: " << ctx_result.error().message << std::endl;
        return 1;
    }

    // Select and run mode
    auto runner = ea::app::IRunner::create();
    int rc = runner->run(ctx_result.value());

    EA_INFO("embedded-agent shutting down");
    ea::trace::shutdown();
    return rc;
}
