#include "Config.h"
#include "common/io/FileSystem.h"
#include "common/io/Logger.h"
#include <toml.hpp>
#include <cstdlib>

namespace ea::config {

Result<AppConfig> load(const std::string& config_path) {
    AppConfig cfg;

    std::string path = config_path;
    if (path.empty()) {
        const char* env_path = getenv("EMBEDDED_AGENT_CONFIG");
        if (env_path && env_path[0] != '\0') {
            path = env_path;
        } else {
            auto cfg_dir = fs::config_dir();
            if (cfg_dir.ok()) {
                path = cfg_dir.value() + "/config.toml";
            }
        }
    }

    cfg.config_path = path;

    auto file_exists = fs::exists(path);
    if (!file_exists.ok() || !file_exists.value()) {
        EA_WARN("Config file not found: {}, using defaults", path);
        const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
        if (api_key) cfg.provider.api_key = api_key;
        const char* model = getenv("EMBEDDED_AGENT_MODEL");
        if (model) cfg.agent.model = model;
        return cfg;
    }

    try {
        auto data = toml::parse(path);

        if (data.contains("agent")) {
            auto agent = toml::find(data, "agent");
            cfg.agent.model = toml::find_or<std::string>(agent, "model", cfg.agent.model);
            cfg.agent.max_iterations = toml::find_or<int>(agent, "max_iterations", cfg.agent.max_iterations);
            cfg.agent.auto_memory = toml::find_or<bool>(agent, "auto_memory", cfg.agent.auto_memory);
            cfg.agent.soul = toml::find_or<std::string>(agent, "soul", cfg.agent.soul);
            cfg.agent.stream = toml::find_or<bool>(agent, "stream", cfg.agent.stream);
            if (agent.contains("compression")) {
                auto compression = toml::find(agent, "compression");
                cfg.agent.compression_enable = toml::find_or<bool>(compression, "enable", cfg.agent.compression_enable);
                cfg.agent.compression_max_tokens = toml::find_or<int>(compression, "max_tokens", cfg.agent.compression_max_tokens);
                cfg.agent.compression_keep_recent_turns = toml::find_or<int>(compression, "keep_recent_turns", cfg.agent.compression_keep_recent_turns);
            }
            if (agent.contains("subagents")) {
                auto subs = toml::find<std::vector<toml::value>>(agent, "subagents");
                for (const auto& sub_val : subs) {
                    const auto& sub = sub_val.as_table();
                    agent::SubagentConfig sc;
                    sc.name = toml::find<std::string>(sub_val, "name");
                    sc.description = toml::find<std::string>(sub_val, "description");
                    sc.model = toml::find_or<std::string>(sub_val, "model", "");
                    sc.system_prompt = toml::find_or<std::string>(sub_val, "system_prompt", "");
                    if (sub.find("toolsets") != sub.end()) {
                        sc.toolsets = toml::find<std::vector<std::string>>(sub_val, "toolsets");
                    }
                    sc.shared_memory = toml::find_or<bool>(sub_val, "shared_memory", true);
                    sc.max_iterations = toml::find_or<int>(sub_val, "max_iterations", 20);
                    sc.dangerous = toml::find_or<bool>(sub_val, "dangerous", false);
                    cfg.agent.subagents.push_back(std::move(sc));
                }
            }
        }

        if (data.contains("provider")) {
            auto provider = toml::find(data, "provider");
            cfg.provider.type = toml::find_or<std::string>(provider, "type", cfg.provider.type);
            cfg.provider.base_url = toml::find_or<std::string>(provider, "base_url", cfg.provider.base_url);
            cfg.provider.api_key = toml::find_or<std::string>(provider, "api_key", cfg.provider.api_key);
            cfg.provider.default_model = toml::find_or<std::string>(provider, "default_model", cfg.provider.default_model);
            cfg.provider.timeout = std::chrono::milliseconds(
                toml::find_or<int>(provider, "timeout", 60) * 1000);
        }

        if (data.contains("memory")) {
            auto memory = toml::find(data, "memory");
            cfg.memory.backend = toml::find_or<std::string>(memory, "backend", cfg.memory.backend);
            cfg.memory.path = toml::find_or<std::string>(memory, "path", cfg.memory.path);
            cfg.memory.enable_fts5 = toml::find_or<bool>(memory, "enable_fts5", cfg.memory.enable_fts5);
            if (memory.contains("strategy")) {
                auto strategy = toml::find(memory, "strategy");
                cfg.memory_strategy.type = toml::find_or<std::string>(strategy, "type", cfg.memory_strategy.type);
                cfg.memory_strategy.working_turns = toml::find_or<int>(strategy, "working_turns", cfg.memory_strategy.working_turns);
                cfg.memory_strategy.short_term_max = toml::find_or<int>(strategy, "short_term_max", cfg.memory_strategy.short_term_max);
                cfg.memory_strategy.long_term_importance = toml::find_or<int>(strategy, "long_term_importance", cfg.memory_strategy.long_term_importance);
                cfg.memory_strategy.enable_fact_extraction = toml::find_or<bool>(strategy, "enable_fact_extraction", cfg.memory_strategy.enable_fact_extraction);
                cfg.memory_strategy.enable_auto_summarize = toml::find_or<bool>(strategy, "enable_auto_summarize", cfg.memory_strategy.enable_auto_summarize);
            }
        }

        if (data.contains("security")) {
            auto security = toml::find(data, "security");
            cfg.security.autonomy = toml::find_or<std::string>(security, "autonomy", cfg.security.autonomy);
            cfg.security.workspace = toml::find_or<std::string>(security, "workspace", cfg.security.workspace);
            if (security.contains("allowed_commands")) {
                cfg.security.allowed_commands = toml::find<std::vector<std::string>>(security, "allowed_commands");
            }
            cfg.security.approval_timeout = toml::find_or<int>(security, "approval_timeout", cfg.security.approval_timeout);
            if (security.contains("approval")) {
                auto approval = toml::find(security, "approval");
                cfg.security.approval_mode = toml::find_or<std::string>(approval, "mode", cfg.security.approval_mode);
                cfg.security.auto_approve_dangerous = toml::find_or<bool>(approval, "auto_approve_dangerous", cfg.security.auto_approve_dangerous);
            }
        }

        if (data.contains("mcp")) {
            auto mcp = toml::find(data, "mcp");
            if (mcp.contains("servers")) {
                auto servers = toml::find<std::vector<toml::value>>(mcp, "servers");
                for (const auto& server_val : servers) {
                    const auto& server = server_val.as_table();
                    McpServerConfig sc;
                    sc.name = toml::find<std::string>(server_val, "name");
                    sc.command = toml::find<std::string>(server_val, "command");
                    if (server.find("args") != server.end()) {
                        sc.args = toml::find<std::vector<std::string>>(server_val, "args");
                    }
                    if (server.find("env") != server.end()) {
                        auto env_table = toml::find<toml::table>(server_val, "env");
                        for (const auto& [k, v] : env_table) {
                            sc.env[k] = v.as_string();
                        }
                    }
                    sc.dangerous = toml::find_or<bool>(server_val, "dangerous", false);
                    cfg.mcp_servers.push_back(std::move(sc));
                }
            }
        }

        if (data.contains("server")) {
            auto server = toml::find(data, "server");
            cfg.server.host = toml::find_or<std::string>(server, "host", cfg.server.host);
            cfg.server.port = toml::find_or<int>(server, "port", cfg.server.port);
            cfg.server.max_sessions = toml::find_or<int>(server, "max_sessions", cfg.server.max_sessions);
            cfg.server.cors_origin = toml::find_or<std::string>(server, "cors_origin", cfg.server.cors_origin);
            cfg.server.session_idle_timeout = toml::find_or<int>(server, "session_idle_timeout", cfg.server.session_idle_timeout);
        }

    } catch (const toml::syntax_error& e) {
        return Error::parse(std::string("TOML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Error::parse(std::string("Config error: ") + e.what());
    }

    const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
    if (api_key && api_key[0] != '\0') cfg.provider.api_key = api_key;
    const char* model = getenv("EMBEDDED_AGENT_MODEL");
    if (model && model[0] != '\0') cfg.agent.model = model;

    return cfg;
}

}  // namespace ea::config
