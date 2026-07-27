#include "Config.h"
#include "TomlConversion.h"
#include "io/FileSystem.h"
#include "log/Logger.h"
#include <toml.hpp>
#include <cstdlib>
#include <filesystem>

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
        cfg = toml::get<AppConfig>(data);
        cfg.config_path = path;
    } catch (const toml::syntax_error& e) {
        return Error::parse(std::string("TOML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Error::parse(std::string("Config error: ") + e.what());
    }

    // Environment variable overrides (take precedence over TOML)
    const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
    if (api_key && api_key[0] != '\0') cfg.provider.api_key = api_key;
    const char* model = getenv("EMBEDDED_AGENT_MODEL");
    if (model && model[0] != '\0') cfg.agent.model = model;

    return cfg;
}

Result<void> save(const AppConfig& cfg, const std::string& config_path) {
    std::string path = config_path.empty() ? cfg.config_path : config_path;
    if (path.empty()) {
        auto cfg_dir = fs::config_dir();
        if (cfg_dir.ok()) {
            path = cfg_dir.value() + "/config.toml";
        } else {
            return cfg_dir.error();
        }
    }

    // Ensure parent directory exists
    auto parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty()) {
        auto mkdir_result = fs::mkdir_p(parent);
        if (!mkdir_result.ok()) {
            return mkdir_result.error();
        }
    }

    try {
        toml::value root = toml::into<AppConfig>::into_toml<toml::type_config>(cfg);
        std::string content = toml::format(root);
        return fs::write_file(path, content);
    } catch (const std::exception& e) {
        return Error::parse(std::string("Config save error: ") + e.what());
    }
}

}  // namespace ea::config
