// AppBuilder.cpp — factory that constructs an AppContext from an AppConfig
// Extracted from main.cpp initialization steps 3~7.6
#include "AppBuilder.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "tool/ShellTool.h"
#include "tool/FileTool.h"
#include "tool/SearchTool.h"
#include "tool/WebTool.h"
#include "tool/MemoryTool.h"
#include "agent/ContextCompressor.h"
#include "agent/ProgressiveMemoryStrategy.h"
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
#include "mcp/McpClient.h"
#include "mcp/StdioTransport.h"
#include "mcp/McpToolAdapter.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "security/StdinApprovalHandler.h"
#include "security/PendingApprovalHandler.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "conversation/SqliteConversationStore.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "common/net/HttpClient.h"
#include "ea/build_config.h"

namespace ea::app {

Result<AppContext> AppBuilder::build(const config::AppConfig& cfg, bool debug) {
    AppContext ctx;
    ctx.config = cfg;
    ctx.debug = debug;

    // 3. Create provider
    auto provider_raw = provider::create(cfg.provider);
    if (!provider_raw) {
        EA_ERROR("Unknown provider type: {}", cfg.provider.type);
        return Error::config("Unknown provider type: " + cfg.provider.type);
    }
    ctx.provider = std::shared_ptr<ea::IProvider>(std::move(provider_raw));

    // 3.5. Create budget tracker (wraps provider if budget is enabled)
    if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
        std::string usage_path = cfg.budget.path;
        if (usage_path.empty()) {
            auto resolved = fs::resolve_data_path("usage.db");
            usage_path = resolved.ok() ? resolved.value() : "usage.db";
        }

        ctx.usage_store = std::make_shared<budget::SqliteUsageStore>(
            budget::SqliteUsageStore::Config{usage_path});
        auto usage_open = ctx.usage_store->open();
        if (!usage_open.ok()) {
            EA_WARN("Usage store open failed: {}", usage_open.error().message);
            ctx.usage_store.reset();
        }

        ctx.budget_tracker = std::make_shared<budget::BudgetTracker>(
            ctx.provider, cfg.budget);
        if (ctx.usage_store) {
            ctx.budget_tracker->set_store(ctx.usage_store);
        }
    }

    // Effective provider: BudgetTracker wraps the real provider if budget is enabled
    ctx.effective_provider = ctx.budget_tracker
        ? static_cast<ea::IProvider*>(ctx.budget_tracker.get())
        : ctx.provider.get();

    // 4. Create memory
    std::string memory_path = cfg.memory.path;
    if (memory_path.empty()) {
        auto resolved = fs::resolve_data_path("memory.db");
        if (resolved.ok()) {
            auto sep = resolved.value().rfind('/');
            if (sep != std::string::npos) {
                fs::mkdir_p(resolved.value().substr(0, sep));
            }
            memory_path = resolved.value();
        } else {
            memory_path = "memory.db";
        }
    }
    ctx.memory = std::make_unique<memory::SqliteMemory>(
        memory::SqliteMemory::Config{memory_path, cfg.memory.enable_fts5});

    // 5. Create security policy
    ctx.security = std::make_unique<security::SecurityPolicy>();
    if (!cfg.security.workspace.empty()) {
        ctx.security->set_workspace(cfg.security.workspace);
    }
    if (!cfg.security.allowed_commands.empty()) {
        ctx.security->set_allowed_commands(cfg.security.allowed_commands);
    }

    // 5.5. Create approval handler
    if (ctx.security->level() == security::AutonomyLevel::Full && cfg.security.auto_approve_dangerous) {
        ctx.approval = nullptr;  // Full mode + auto-approve = no approval needed
    } else if (cfg.security.approval_mode == "auto") {
#if defined(EA_MODE_SERVER)
        ctx.approval = std::make_unique<security::PendingApprovalHandler>(cfg.security.approval_timeout);
#elif defined(EA_MODE_CLI)
        ctx.approval = std::make_unique<security::StdinApprovalHandler>();
#else
        ctx.approval = nullptr;  // Embedded mode: no interactive interface
#endif
    } else if (cfg.security.approval_mode == "stdin") {
        ctx.approval = std::make_unique<security::StdinApprovalHandler>();
    } else if (cfg.security.approval_mode == "pending") {
        ctx.approval = std::make_unique<security::PendingApprovalHandler>(cfg.security.approval_timeout);
    }

    // 5.6. Create context compressor
    if (cfg.agent.compression_enable) {
        agent::CompressionConfig comp_cfg;
        comp_cfg.max_tokens = cfg.agent.compression_max_tokens;
        comp_cfg.keep_recent_turns = cfg.agent.compression_keep_recent_turns;
        ctx.compressor = std::make_unique<agent::ContextCompressor>(ctx.effective_provider, comp_cfg);
    }

    // 5.7. Create memory strategy
    if (cfg.memory_strategy.type == "progressive") {
        agent::ProgressiveMemoryConfig strat_cfg;
        strat_cfg.working_turns = cfg.memory_strategy.working_turns;
        strat_cfg.short_term_max = cfg.memory_strategy.short_term_max;
        strat_cfg.long_term_importance = cfg.memory_strategy.long_term_importance;
        strat_cfg.enable_fact_extraction = cfg.memory_strategy.enable_fact_extraction;
        strat_cfg.enable_auto_summarize = cfg.memory_strategy.enable_auto_summarize;
        ctx.memory_strategy = std::make_unique<agent::ProgressiveMemoryStrategy>(strat_cfg);
    }
    // type == "none" -> strategy stays nullptr -> NullMemoryStrategy behavior (no-op)

    // 5.8. Create conversation store
    std::string conv_path = cfg.conversation.path;
    if (conv_path.empty()) {
        auto resolved = fs::resolve_data_path("conversations.db");
        conv_path = resolved.ok() ? resolved.value() : "conversations.db";
    }
    ctx.conversation_store = std::make_unique<conversation::SqliteConversationStore>(
        conversation::SqliteConversationStore::Config{conv_path});
    auto conv_open = ctx.conversation_store->open();
    if (!conv_open.ok()) {
        EA_ERROR("Conversation store open failed: {}", conv_open.error().message);
        ctx.conversation_store.reset();  // Continue without persistence
    }

    // 6. Create HTTP client for WebTool
    ctx.http_client = net::HttpClient{};

    // 7. Register tools
    ctx.registry = std::make_unique<tool::ToolRegistry>();
    ctx.registry->register_tool(std::make_unique<tool::ShellTool>());
    ctx.registry->register_tool(std::make_unique<tool::FileTool>());
    ctx.registry->register_tool(std::make_unique<tool::SearchTool>());
    ctx.registry->register_tool(std::make_unique<tool::WebTool>(&ctx.http_client));
    ctx.registry->register_tool(std::make_unique<tool::MemoryTool>(ctx.memory.get()));

    // 7.5. Connect MCP servers and register their tools
    for (auto& server_cfg : cfg.mcp_servers) {
        EA_INFO("Connecting MCP server: {}", server_cfg.name);

        mcp::StdioTransport::Config transport_cfg;
        transport_cfg.command = server_cfg.command;
        transport_cfg.args = server_cfg.args;
        transport_cfg.env = server_cfg.env;

        auto transport = std::make_unique<mcp::StdioTransport>(std::move(transport_cfg));
        auto client = std::make_shared<mcp::McpClient>(std::move(transport));

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

        auto toolset = std::make_unique<tool::Toolset>(server_cfg.name);
        for (auto& spec : tools_result.value()) {
            toolset->add(std::make_unique<mcp::McpToolAdapter>(client, spec, server_cfg.dangerous));
        }
        ctx.registry->register_toolset(std::move(toolset));
        ctx.mcp_clients.push_back(client);

        EA_INFO("MCP server '{}' connected with {} tools", server_cfg.name, tools_result.value().size());
    }

    // 7.6. Create subagent orchestrator
    ctx.orchestrator = std::make_unique<agent::SubagentOrchestrator>(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get());

    for (auto& sub_cfg : cfg.agent.subagents) {
        EA_INFO("Registering subagent template: {}", sub_cfg.name);
        ctx.orchestrator->register_template(sub_cfg);
    }

    if (!cfg.agent.subagents.empty()) {
        ctx.registry->register_tool(std::make_unique<agent::DelegateTool>(ctx.orchestrator.get()));
    }

    return ctx;
}

}  // namespace ea::app
