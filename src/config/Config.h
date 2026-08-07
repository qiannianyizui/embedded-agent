#pragma once
#include "base/Result.h"
#include "net/TlsConfig.h"
#include "net/RetryPolicy.h"
#include "agent/SubagentConfig.h"
#include "budget/Types.h"
#include <map>
#include <string>
#include <vector>
#include <chrono>

namespace ea::config {

struct LogConfig {
    std::string level = "debug";                      // error | warn | info | debug
    bool persist = true;                              // 是否持久化日志到磁盘
    bool verbose = false;                             // 终端也输出日志
    int max_file_bytes = 10 * 1024 * 1024;           // 单个日志文件最大 10MB
    int max_total_bytes = 100 * 1024 * 1024;         // 日志目录最大总占用 100MB
};

struct TraceConfig {
    std::string persist = "rolling";                  // none | rolling | full
    int max_entries = 10000;                          // rolling 策略最大行数
    std::string tool_io = "redacted";                 // off | redacted | full
    int tool_io_truncate_bytes = 40960;               // 工具 I/O 截断字节数 (40KB)
    std::string llm_payload = "off";                  // off | redacted | full
};

struct ProviderConfig {
    std::string type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string default_model;
    net::TlsConfig tls;
    net::RetryPolicy retry;
    std::chrono::milliseconds timeout{60000};
};

struct MemoryStrategyConfig {
    std::string type = "progressive";       // "progressive" | "none"
    int working_turns = 6;
    int short_term_max = 20;
    int long_term_importance = 8;
    bool enable_fact_extraction = true;
    bool enable_auto_summarize = true;
};

struct MemoryConfig {
    std::string path;               // SQLite database path (empty = auto)
    bool enable_wal = true;         // WAL mode for file-based DBs
    double trust_positive = 0.05;   // Trust delta for helpful feedback
    double trust_negative = -0.10;  // Trust delta for unhelpful feedback
    int hrr_dim = 1024;            // HRR vector dimension
    // Nested strategy config — corresponds to [memory.strategy]
    MemoryStrategyConfig strategy;
};

struct SecurityConfig {
    std::string autonomy = "supervised";
    std::vector<std::string> allowed_commands;
    std::string workspace;
    int approval_timeout = 300;              // Approval timeout in seconds (pending approval mode)
    struct Approval {                        // corresponds to [security.approval]
        std::string mode = "auto";           // stdin / pending / auto
        bool auto_approve_dangerous = false; // Only effective in Full autonomy
    } approval;
};

struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    int context_file_max_chars = 20000;  // Context file char limit
    // Context compression — corresponds to [agent.compression]
    struct Compression {
        bool enable = true;
        int max_tokens = 8000;
        int keep_recent_turns = 4;
    } compression;
    // Subagent delegation
    std::vector<agent::SubagentConfig> subagents;
    // Streaming output
    bool stream = true;
};

struct SkillConfig {
    bool enable = true;                  // [skills] enable = true
    std::vector<std::string> dirs;       // Extra skill directories
    std::vector<std::string> disabled;   // Skill names to hide from index/tools
    bool template_vars = true;           // Replace ${EA_SKILL_DIR}/${EA_SKILL_NAME}
    bool inline_shell = false;           // Execute !`cmd` snippets in SKILL.md
    int inline_shell_timeout = 10;       // Seconds per inline shell snippet
};

struct McpServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool dangerous = false;
};

struct ConversationConfig {
    std::string path;             // conversations.db path (empty = auto)
    bool auto_resume = true;      // CLI: auto-resume last conversation
    bool auto_persist = true;     // Auto-save messages each turn
    int max_conversations = 1000; // Max stored conversations
};

struct AppConfig {
    LogConfig log;
    TraceConfig trace;
    ProviderConfig provider;
    MemoryConfig memory;
    ConversationConfig conversation;
    budget::BudgetConfig budget;
    SecurityConfig security;
    AgentConfig agent;
    SkillConfig skills;
    std::vector<McpServerConfig> mcp_servers;
    std::string config_path;
};

Result<AppConfig> load(const std::string& config_path = "");

// Save AppConfig to a TOML file. If config_path is empty, uses cfg.config_path
// (the path it was loaded from). Creates parent directories as needed.
Result<void> save(const AppConfig& cfg, const std::string& config_path = "");

}  // namespace ea::config
