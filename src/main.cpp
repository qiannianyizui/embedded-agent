#include "config/Config.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "tool/ShellTool.h"
#include "tool/FileTool.h"
#include "tool/SearchTool.h"
#include "tool/WebTool.h"
#include "tool/MemoryTool.h"
#include "agent/AgentLoop.h"
#include "platform/Platform.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "security/StdinApprovalHandler.h"
#include "security/PendingApprovalHandler.h"
#include "ea/build_config.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "common/net/HttpClient.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent for Linux & Android"};

    std::string config_path;
    bool debug = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("--debug", debug, "Enable debug logging");

    CLI11_PARSE(app, argc, argv);

    // 1. Initialize logging
    auto home = ea::platform::home_dir();
    ea::log::init(home + "/.embedded-agent", debug);

    EA_INFO("embedded-agent v0.1.0 starting");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // 2. Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }
    auto cfg = cfg_result.value();

    // 3. Create provider
    auto provider = ea::provider::create(cfg.provider);
    if (!provider) {
        EA_ERROR("Unknown provider type: {}", cfg.provider.type);
        std::cerr << "Unknown provider type: " << cfg.provider.type << std::endl;
        return 1;
    }

    // 4. Create memory
    std::string memory_path = cfg.memory.path;
    if (memory_path.empty()) {
        auto data_dir = ea::fs::config_dir();
        if (data_dir.ok()) {
            ea::fs::mkdir_p(data_dir.value());
            memory_path = data_dir.value() + "/memory.db";
        } else {
            memory_path = home + "/.embedded-agent/memory.db";
        }
    }
    auto memory = std::make_unique<ea::memory::SqliteMemory>(
        ea::memory::SqliteMemory::Config{memory_path, cfg.memory.enable_fts5});

    // 5. Create security policy
    auto security = std::make_unique<ea::security::SecurityPolicy>();
    if (!cfg.security.workspace.empty()) {
        security->set_workspace(cfg.security.workspace);
    }
    if (!cfg.security.allowed_commands.empty()) {
        security->set_allowed_commands(cfg.security.allowed_commands);
    }

    // 5.5. Create approval handler
    std::unique_ptr<ea::security::IApprovalHandler> approval;

    if (security->level() == ea::security::AutonomyLevel::Full && cfg.security.auto_approve_dangerous) {
        approval = nullptr;  // Full mode + auto-approve = no approval needed
    } else if (cfg.security.approval_mode == "auto") {
#ifdef EA_MODE_CLI
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
#elif defined(EA_MODE_SERVER)
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
#else
        approval = nullptr;  // Embedded mode: no interactive interface
#endif
    } else if (cfg.security.approval_mode == "stdin") {
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
    } else if (cfg.security.approval_mode == "pending") {
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
    }

    // 6. Create HTTP client for WebTool
    ea::net::HttpClient http_client;

    // 7. Register tools
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<ea::tool::ShellTool>());
    registry.register_tool(std::make_unique<ea::tool::FileTool>());
    registry.register_tool(std::make_unique<ea::tool::SearchTool>());
    registry.register_tool(std::make_unique<ea::tool::WebTool>(&http_client));
    registry.register_tool(std::make_unique<ea::tool::MemoryTool>(memory.get()));

    // 8. Create agent loop
    ea::agent::AgentLoop loop(
        provider.get(), &registry, memory.get(),
        ea::agent::AgentLoop::Config{cfg.agent.max_iterations},
        [](const std::string& text) {
            std::cout << text << std::endl;
        },
        security.get(),
        approval.get()
    );

    // 9. Interactive loop
    std::string input;
    std::cout << "embedded-agent v0.1.0 (type /quit to exit)" << std::endl;

    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit" || input == "/exit") break;
        if (input.empty()) continue;

        auto result = loop.run(input);
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }

    EA_INFO("embedded-agent shutting down");
    return 0;
}
