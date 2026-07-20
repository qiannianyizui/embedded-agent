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
#include "agent/LoggingEventListener.h"
#include "agent/ContextCompressor.h"
#include "agent/ProgressiveMemoryStrategy.h"
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
#include "mcp/McpClient.h"
#include "mcp/StdioTransport.h"
#include "mcp/McpToolAdapter.h"
#include "platform/Platform.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "security/StdinApprovalHandler.h"
#include "security/PendingApprovalHandler.h"
#include "ea/build_config.h"
#include "conversation/SqliteConversationStore.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "common/net/HttpClient.h"
#include "server/HttpServer.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>

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
    auto provider_raw = ea::provider::create(cfg.provider);
    if (!provider_raw) {
        EA_ERROR("Unknown provider type: {}", cfg.provider.type);
        std::cerr << "Unknown provider type: " << cfg.provider.type << std::endl;
        return 1;
    }
    auto provider = std::shared_ptr<ea::IProvider>(std::move(provider_raw));

    // 3.5. Create budget tracker (wraps provider if budget is enabled)
    std::shared_ptr<ea::budget::SqliteUsageStore> usage_store;
    std::shared_ptr<ea::budget::BudgetTracker> budget_tracker;

    if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
        std::string usage_path = cfg.budget.path;
        if (usage_path.empty()) {
            auto data_dir = ea::fs::config_dir();
            if (data_dir.ok()) {
                usage_path = data_dir.value() + "/usage.db";
            } else {
                usage_path = home + "/.embedded-agent/usage.db";
            }
        }

        usage_store = std::make_shared<ea::budget::SqliteUsageStore>(
            ea::budget::SqliteUsageStore::Config{usage_path});
        auto usage_open = usage_store->open();
        if (!usage_open.ok()) {
            EA_WARN("Usage store open failed: {}", usage_open.error().message);
            usage_store.reset();
        }

        budget_tracker = std::make_shared<ea::budget::BudgetTracker>(
            provider, cfg.budget);
        if (usage_store) {
            budget_tracker->set_store(usage_store);
        }
    }

    // Effective provider: BudgetTracker wraps the real provider if budget is enabled
    ea::IProvider* effective_provider = budget_tracker
        ? static_cast<ea::IProvider*>(budget_tracker.get())
        : provider.get();

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
#if defined(EA_MODE_SERVER)
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
#elif defined(EA_MODE_CLI)
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
#else
        approval = nullptr;  // Embedded mode: no interactive interface
#endif
    } else if (cfg.security.approval_mode == "stdin") {
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
    } else if (cfg.security.approval_mode == "pending") {
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
    }

    // 5.6. Create context compressor
    std::unique_ptr<ea::agent::ContextCompressor> compressor;
    if (cfg.agent.compression_enable) {
        ea::agent::CompressionConfig comp_cfg;
        comp_cfg.max_tokens = cfg.agent.compression_max_tokens;
        comp_cfg.keep_recent_turns = cfg.agent.compression_keep_recent_turns;
        compressor = std::make_unique<ea::agent::ContextCompressor>(effective_provider, comp_cfg);
    }

    // 5.7. Create memory strategy
    std::unique_ptr<ea::agent::IMemoryStrategy> strategy;
    if (cfg.memory_strategy.type == "progressive") {
        ea::agent::ProgressiveMemoryConfig strat_cfg;
        strat_cfg.working_turns = cfg.memory_strategy.working_turns;
        strat_cfg.short_term_max = cfg.memory_strategy.short_term_max;
        strat_cfg.long_term_importance = cfg.memory_strategy.long_term_importance;
        strat_cfg.enable_fact_extraction = cfg.memory_strategy.enable_fact_extraction;
        strat_cfg.enable_auto_summarize = cfg.memory_strategy.enable_auto_summarize;
        strategy = std::make_unique<ea::agent::ProgressiveMemoryStrategy>(strat_cfg);
    }
    // type == "none" → strategy stays nullptr → NullMemoryStrategy behavior (no-op)

    // 5.8. Create conversation store
    std::unique_ptr<ea::conversation::SqliteConversationStore> conv_store;
    std::string conv_path = cfg.conversation.path;
    if (conv_path.empty()) {
        auto data_dir = ea::fs::config_dir();
        if (data_dir.ok()) {
            conv_path = data_dir.value() + "/conversations.db";
        } else {
            conv_path = home + "/.embedded-agent/conversations.db";
        }
    }
    conv_store = std::make_unique<ea::conversation::SqliteConversationStore>(
        ea::conversation::SqliteConversationStore::Config{conv_path});
    auto conv_open = conv_store->open();
    if (!conv_open.ok()) {
        EA_ERROR("Conversation store open failed: {}", conv_open.error().message);
        conv_store.reset();  // Continue without persistence
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

    // 7.5. Connect MCP servers and register their tools
    std::vector<std::shared_ptr<ea::mcp::McpClient>> mcp_clients;

    for (auto& server_cfg : cfg.mcp_servers) {
        EA_INFO("Connecting MCP server: {}", server_cfg.name);

        ea::mcp::StdioTransport::Config transport_cfg;
        transport_cfg.command = server_cfg.command;
        transport_cfg.args = server_cfg.args;
        transport_cfg.env = server_cfg.env;

        auto transport = std::make_unique<ea::mcp::StdioTransport>(std::move(transport_cfg));
        auto client = std::make_shared<ea::mcp::McpClient>(std::move(transport));

        auto conn_result = client->connect();
        if (!conn_result.ok()) {
            EA_ERROR("MCP server '{}' connection failed: {}", server_cfg.name, conn_result.error().message);
            continue;
        }

        auto tools_result = client->list_tools();
        if (!tools_result.ok()) {
            EA_ERROR("MCP server '{}' list_tools failed: {}", server_cfg.name, tools_result.error().message);
            client->disconnect();
            continue;
        }

        auto toolset = std::make_unique<ea::tool::Toolset>(server_cfg.name);
        for (auto& spec : tools_result.value()) {
            toolset->add(std::make_unique<ea::mcp::McpToolAdapter>(client, spec, server_cfg.dangerous));
        }
        registry.register_toolset(std::move(toolset));
        mcp_clients.push_back(client);

        EA_INFO("MCP server '{}' connected with {} tools", server_cfg.name, tools_result.value().size());
    }

    // 7.6. Create subagent orchestrator
    auto orchestrator = std::make_unique<ea::agent::SubagentOrchestrator>(
        effective_provider, &registry, memory.get());

    for (auto& sub_cfg : cfg.agent.subagents) {
        EA_INFO("Registering subagent template: {}", sub_cfg.name);
        orchestrator->register_template(sub_cfg);
    }

    if (!cfg.agent.subagents.empty()) {
        registry.register_tool(std::make_unique<ea::agent::DelegateTool>(orchestrator.get()));
    }

    // 8. Create agent loop
    ea::agent::AgentLoop::StreamFn stream_fn;

    if (cfg.agent.stream) {
        stream_fn = [](const ea::StreamChunk& chunk) {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                std::cout << chunk.data << std::flush;
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                std::cout << std::endl;
            }
            // ToolCallBegin/Delta/End are silent in CLI mode
            // Server mode can forward these via WebSocket
        };
    }

    ea::agent::AgentLoop loop(
        effective_provider, &registry, memory.get(),
        ea::agent::AgentLoop::Config{
            cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream, cfg.conversation.auto_persist
        },
        [](const std::string& text) { std::cout << text << std::endl; },
        stream_fn,
        security.get(),
        approval.get(),
        compressor.get(),
        strategy.get(),
        conv_store.get(),
        budget_tracker.get()
    );

    // 8.5. Add event listeners
    if (debug) {
        loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }

    // Budget event listener — prints usage after each LLM call
    struct BudgetEventListener : public ea::agent::IEventListener {
        ea::budget::BudgetTracker* tracker_;
        explicit BudgetEventListener(ea::budget::BudgetTracker* t) : tracker_(t) {}
        void on_event(const ea::agent::AgentEvent& event) override {
            if (event.type == ea::agent::AgentEventType::LLMResponse) {
                if (event.usage.input_tokens > 0 || event.usage.output_tokens > 0) {
                    auto su = tracker_->session_usage();
                    auto sc = tracker_->session_cost();
                    std::cout << "\n[Usage: " << su.input_tokens << " in / "
                              << su.output_tokens << " out | $"
                              << std::fixed << std::setprecision(4) << sc.total()
                              << " session]" << std::flush;
                }
            }
        }
    };
    if (budget_tracker) {
        loop.add_listener(std::make_shared<BudgetEventListener>(budget_tracker.get()));
    }

    // 9. Run mode
#ifdef EA_MODE_SERVER
    // Server mode: start HTTP API
    ea::server::ServerConfig srv_cfg;
    srv_cfg.host = cfg.server.host;
    srv_cfg.port = cfg.server.port;
    srv_cfg.max_sessions = cfg.server.max_sessions;
    srv_cfg.cors_origin = cfg.server.cors_origin;
    srv_cfg.session_idle_timeout = std::chrono::seconds(cfg.server.session_idle_timeout);

    auto http_server = std::make_unique<ea::server::HttpServer>(
        srv_cfg, effective_provider, &registry, security.get(), memory.get(), conv_store.get()
    );

    EA_INFO("Server starting on {}:{}", srv_cfg.host, srv_cfg.port);
    http_server->start();
#else
    // CLI mode: interactive loop
    std::string input;
    std::cout << "embedded-agent v0.1.0 (type /quit to exit)" << std::endl;

    // Auto-resume last conversation
    if (conv_store && cfg.conversation.auto_resume) {
        auto recent = conv_store->list(1, 0);
        if (recent.ok() && !recent.value().empty()) {
            auto& meta = recent.value()[0];
            auto msgs = conv_store->load(meta.id);
            if (msgs.ok() && !msgs.value().empty()) {
                loop.restore_conversation(meta.id, std::move(msgs.value()));
                std::cout << "Resumed: " << meta.title
                          << " (" << meta.message_count << " messages)" << std::endl;
            }
        }
    }

    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit" || input == "/exit") break;
        if (input.empty()) continue;

        if (input == "/usage") {
            if (budget_tracker) {
                auto su = budget_tracker->session_usage();
                auto gu = budget_tracker->global_usage();
                std::cout << "Session Usage:\n"
                          << "  Input:  " << su.input_tokens << " tokens\n"
                          << "  Output: " << su.output_tokens << " tokens\n"
                          << "  Cache:  " << su.cache_read_tokens << " read / "
                          << su.cache_write_tokens << " write\n"
                          << "  Total:  " << su.total_tokens() << " tokens\n\n"
                          << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            } else {
                std::cout << "Budget tracking not enabled" << std::endl;
            }
            continue;
        }
        if (input == "/cost") {
            if (budget_tracker) {
                auto sc = budget_tracker->session_cost();
                auto gc = budget_tracker->global_cost();
                std::cout << "Session Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << sc.total() << "\n\n"
                          << "Global Cost:\n"
                          << "  Total: $" << gc.total() << std::endl;
            } else {
                std::cout << "Budget tracking not enabled" << std::endl;
            }
            continue;
        }
        if (input == "/usage global") {
            if (budget_tracker) {
                auto gu = budget_tracker->global_usage();
                std::cout << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            }
            continue;
        }
        if (input == "/cost global") {
            if (budget_tracker) {
                auto gc = budget_tracker->global_cost();
                std::cout << "Global Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << gc.total() << std::endl;
            }
            continue;
        }
        if (input == "/history") {
            if (conv_store) {
                auto list = conv_store->list(10, 0);
                if (list.ok()) {
                    for (const auto& m : list.value()) {
                        std::cout << "  " << m.id << "  " << m.title
                                  << "  (" << m.message_count << " msgs, " << m.updated_at << ")" << std::endl;
                    }
                }
            } else {
                std::cout << "Conversation persistence not available" << std::endl;
            }
            continue;
        }
        if (input.substr(0, 8) == "/resume ") {
            if (conv_store) {
                std::string cid = input.substr(8);
                auto msgs = conv_store->load(cid);
                if (msgs.ok()) {
                    loop.restore_conversation(cid, std::move(msgs.value()));
                    auto meta = conv_store->get_meta(cid);
                    std::cout << "Resumed: " << (meta.ok() ? meta.value().title : cid) << std::endl;
                } else {
                    std::cout << "Conversation not found: " << cid << std::endl;
                }
            }
            continue;
        }
        if (input == "/export") {
            if (conv_store && !loop.conversation_id().empty()) {
                auto data = conv_store->export_jsonl(loop.conversation_id());
                if (data.ok()) {
                    std::cout << data.value();
                } else {
                    std::cout << "Export failed: " << data.error().message << std::endl;
                }
            } else {
                std::cout << "No active conversation to export" << std::endl;
            }
            continue;
        }
        if (input.substr(0, 8) == "/import ") {
            if (conv_store) {
                std::string filepath = input.substr(8);
                std::ifstream file(filepath);
                if (!file.is_open()) {
                    std::cout << "Cannot open file: " << filepath << std::endl;
                    continue;
                }
                std::string jsonl_data((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                auto new_id = conv_store->import_jsonl(jsonl_data);
                if (new_id.ok()) {
                    std::cout << "Imported conversation: " << new_id.value() << std::endl;
                } else {
                    std::cout << "Import failed: " << new_id.error().message << std::endl;
                }
            }
            continue;
        }

        auto result = loop.run(input);
        if (budget_tracker && !loop.conversation_id().empty()) {
            budget_tracker->set_session_id(loop.conversation_id());
        }
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }
#endif

    EA_INFO("embedded-agent shutting down");
    return 0;
}
