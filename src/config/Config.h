#pragma once
#include "common/base/Result.h"
#include "common/net/TlsConfig.h"
#include "common/net/RetryPolicy.h"
#include "agent/SubagentConfig.h"
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace ea::config {

struct ProviderConfig {
    std::string type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string default_model;
    net::TlsConfig tls;
    net::RetryPolicy retry;
    std::chrono::milliseconds timeout{60000};
};

struct MemoryConfig {
    std::string backend = "sqlite";
    std::string path;
    bool enable_fts5 = true;
};

struct SecurityConfig {
    std::string autonomy = "supervised";
    std::vector<std::string> allowed_commands;
    std::string workspace;
    int approval_timeout = 300;              // Approval timeout in seconds (Server mode)
    std::string approval_mode = "auto";      // stdin / pending / auto
    bool auto_approve_dangerous = false;     // Only effective in Full autonomy
};

struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    // Context compression
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
    // Subagent delegation
    std::vector<agent::SubagentConfig> subagents;
    // Streaming output
    bool stream = true;
};

struct McpServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool dangerous = false;
};

struct AppConfig {
    ProviderConfig provider;
    MemoryConfig memory;
    SecurityConfig security;
    AgentConfig agent;
    std::vector<McpServerConfig> mcp_servers;
    std::string config_path;
};

Result<AppConfig> load(const std::string& config_path = "");

}  // namespace ea::config
