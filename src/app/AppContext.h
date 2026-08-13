// AppContext — holds all runtime objects for the application
#pragma once
#include "config/Config.h"
#include "provider/IProvider.h"
#include "memory/HolographicMemory.h"
#include "memory/CuratedMemoryStore.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "agent/IMemoryStrategy.h"
#include "agent/SubagentOrchestrator.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "conversation/SqliteConversationStore.h"
#include "mcp/McpClient.h"
#include "skill/Skill.h"
#include "tool/WebSearch.h"
#include "net/HttpClient.h"
#include <memory>
#include <vector>
#include <string>

namespace ea::app {

struct AppContext {
    config::AppConfig config;

    // Provider layer
    std::shared_ptr<ea::IProvider> provider;
    std::shared_ptr<ea::budget::BudgetTracker> budget_tracker;
    ea::IProvider* effective_provider = nullptr;

    // Storage
    std::unique_ptr<ea::memory::HolographicMemory> memory;
    std::unique_ptr<ea::memory::CuratedMemoryStore> curated_memory;
    std::unique_ptr<ea::conversation::SqliteConversationStore> conversation_store;
    std::shared_ptr<ea::budget::SqliteUsageStore> usage_store;

    // Security
    std::unique_ptr<ea::security::SecurityPolicy> security;
    std::unique_ptr<ea::security::IApprovalHandler> approval;

    // Agent components
    std::unique_ptr<ea::agent::ContextCompressor> compressor;
    std::unique_ptr<ea::agent::IMemoryStrategy> memory_strategy;
    std::unique_ptr<ea::agent::SubagentOrchestrator> orchestrator;

    // Tools
    std::unique_ptr<ea::tool::ToolRegistry> registry;
    ea::net::HttpClient http_client;

    // Skills
    std::unique_ptr<ea::skill::SkillManager> skills;
    std::unique_ptr<ea::skill::PluginManager> plugins;
    std::string skills_index;   // Rendered <available_skills> block for system prompt

    // Web search
    std::unique_ptr<ea::tool::IWebSearchBackend> web_search;

    // MCP
    std::vector<std::shared_ptr<ea::mcp::McpClient>> mcp_clients;

    // Debug
    bool debug = false;

    // Context files
    std::string soul;              // Identity text (from SOUL.md or config)
    std::string context_files;     // Project context file content
};

}  // namespace ea::app
