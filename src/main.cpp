#include "app/AppBuilder.h"
#include "app/IRunner.h"
#include "app/SetupWizardRunner.h"
#include "platform/Platform.h"
#include "io/FileSystem.h"
#include "log/Logger.h"
#include "config/Config.h"
#include "trace/Trace.h"
#include <CLI/CLI.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent"};

    std::string config_path;
    bool verbose = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("-v,--verbose", verbose, "Output logs to terminal");

    // --- Mode subcommands ---
    auto cli_cmd    = app.add_subcommand("cli",    "Run in CLI mode (terminal REPL)");
    auto tui_cmd    = app.add_subcommand("tui",    "Run in TUI mode (FTXUI interface)");
    auto server_cmd = app.add_subcommand("server", "Run as HTTP API server");

    // Server-specific options
    std::string server_host;
    int server_port = 0;
    server_cmd->add_option("--host", server_host, "Server bind host (overrides config)");
    server_cmd->add_option("--port", server_port, "Server bind port (overrides config)");

    // Setup subcommand
    auto setup_cmd = app.add_subcommand("setup", "Interactive setup wizard");
    bool setup_non_interactive = false;
    bool setup_reset = false;
    setup_cmd->add_flag("--non-interactive", setup_non_interactive,
                        "Non-interactive mode (use defaults/env vars)");
    setup_cmd->add_flag("--reset", setup_reset, "Reset config to defaults");

    // Allow 0 or 1 subcommand (0 = default mode)
    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    // Handle setup subcommand first (it has its own init path)
    if (setup_cmd->parsed()) {
        return ea::app::SetupWizardRunner::run({
            setup_non_interactive, setup_reset, config_path
        });
    }

    // Determine run mode from subcommand (or default)
    ea::app::RunMode run_mode;
    if (server_cmd->parsed()) {
        run_mode = ea::app::RunMode::Server;
    } else if (tui_cmd->parsed()) {
        run_mode = ea::app::RunMode::Tui;
    } else if (cli_cmd->parsed()) {
        run_mode = ea::app::RunMode::Cli;
    } else {
        // No subcommand: default to TUI
        run_mode = ea::app::RunMode::Tui;
    }

    // Initialize logging with defaults (before config is loaded)
    auto cfg_dir = ea::fs::config_dir();
    std::string log_dir = cfg_dir.ok() ? cfg_dir.value() + "/logs" : "/tmp/.embedded-agent/logs";
    ea::log::Config log_cfg;
    log_cfg.log_dir = log_dir;
    log_cfg.verbose = verbose;
    ea::log::init(log_cfg);
    EA_INFO("embedded-agent v0.1.0 starting (mode: {})",
            run_mode == ea::app::RunMode::Server ? "server" :
            run_mode == ea::app::RunMode::Tui    ? "tui" : "cli");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }

    // Apply server CLI overrides
    if (run_mode == ea::app::RunMode::Server) {
        if (!server_host.empty()) cfg_result.value().server.host = server_host;
        if (server_port > 0)     cfg_result.value().server.port = server_port;
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
    auto ctx_result = ea::app::AppBuilder::build(cfg_result.value(), verbose, run_mode);
    if (!ctx_result.ok()) {
        EA_ERROR("App init failed: {}", ctx_result.error().message);
        std::cerr << "Init error: " << ctx_result.error().message << std::endl;
        return 1;
    }

    // Select and run mode
    auto runner = ea::app::IRunner::create(run_mode);
    int rc = runner->run(ctx_result.value());

    EA_INFO("embedded-agent shutting down");
    ea::trace::shutdown();
    return rc;
}
