#include "app/AppBuilder.h"
#include "app/IRunner.h"
#include "app/SetupWizardRunner.h"
#include "platform/Platform.h"
#include "common/io/FileSystem.h"
#include "common/io/Logger.h"
#include "config/Config.h"
#include <CLI/CLI.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent"};

    std::string config_path;
    bool debug = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("--debug", debug, "Enable debug logging");

    // Setup subcommand
    auto setup_cmd = app.add_subcommand("setup", "Interactive setup wizard");
    bool setup_non_interactive = false;
    bool setup_reset = false;
    setup_cmd->add_flag("--non-interactive", setup_non_interactive,
                        "Non-interactive mode (use defaults/env vars)");
    setup_cmd->add_flag("--reset", setup_reset, "Reset config to defaults");

    CLI11_PARSE(app, argc, argv);

    // Handle setup subcommand
    if (setup_cmd->parsed()) {
        return ea::app::SetupWizardRunner::run({
            setup_non_interactive, setup_reset, config_path
        });
    }

    // Initialize logging
    auto cfg_dir = ea::fs::config_dir();
    ea::log::init(cfg_dir.ok() ? cfg_dir.value() : "/tmp/.embedded-agent", debug);
    EA_INFO("embedded-agent v0.1.0 starting");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }

    // Build application context
    auto ctx_result = ea::app::AppBuilder::build(cfg_result.value(), debug);
    if (!ctx_result.ok()) {
        EA_ERROR("App init failed: {}", ctx_result.error().message);
        std::cerr << "Init error: " << ctx_result.error().message << std::endl;
        return 1;
    }

    // Select and run mode
    auto runner = ea::app::IRunner::create();
    int rc = runner->run(ctx_result.value());

    EA_INFO("embedded-agent shutting down");
    return rc;
}
