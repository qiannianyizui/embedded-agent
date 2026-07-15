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
        }

        if (data.contains("security")) {
            auto security = toml::find(data, "security");
            cfg.security.autonomy = toml::find_or<std::string>(security, "autonomy", cfg.security.autonomy);
            cfg.security.workspace = toml::find_or<std::string>(security, "workspace", cfg.security.workspace);
            if (security.contains("allowed_commands")) {
                cfg.security.allowed_commands = toml::find<std::vector<std::string>>(security, "allowed_commands");
            }
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
