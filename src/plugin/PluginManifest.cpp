#include "PluginManifest.h"
#include "common/io/FileSystem.h"
#include <toml.hpp>

namespace ea::plugin {

Result<PluginManifest> parse_manifest(const std::string& dir_path) {
    // Check that the plugin directory exists
    auto dir_exists = fs::exists(dir_path);
    if (!dir_exists.ok() || !dir_exists.value()) {
        return Error::not_found("Plugin directory not found: " + dir_path);
    }

    std::string toml_path = dir_path + "/plugin.toml";
    auto file_exists = fs::exists(toml_path);
    if (!file_exists.ok() || !file_exists.value()) {
        return Error::not_found("plugin.toml not found in: " + dir_path);
    }

    toml::value data;
    try {
        data = toml::parse(toml_path);
    } catch (const toml::syntax_error& e) {
        return Error::parse(std::string("TOML syntax error in plugin.toml: ") + e.what());
    } catch (const std::exception& e) {
        return Error::parse(std::string("Failed to parse plugin.toml: ") + e.what());
    }

    PluginManifest manifest;
    manifest.path = dir_path;

    try {
        // Required: [plugin] section
        if (!data.contains("plugin")) {
            return Error::config("Missing [plugin] section in plugin.toml");
        }
        auto plugin = toml::find(data, "plugin");

        // Required fields
        manifest.info.name = toml::find<std::string>(plugin, "name");
        manifest.info.version = toml::find<std::string>(plugin, "version");
        manifest.info.api_version = toml::find<int>(plugin, "api_version");
        manifest.library = toml::find<std::string>(plugin, "library");

        // Validate api_version
        if (manifest.info.api_version != EA_PLUGIN_API_VERSION) {
            return Error::config(
                "api_version mismatch: expected " + std::to_string(EA_PLUGIN_API_VERSION) +
                ", got " + std::to_string(manifest.info.api_version));
        }

        // Optional fields
        manifest.info.description = toml::find_or<std::string>(plugin, "description", "");

        // Optional: provides array
        if (plugin.contains("provides")) {
            manifest.info.provides = toml::find<std::vector<std::string>>(plugin, "provides");
        }

        // Optional: depends array
        if (plugin.contains("depends")) {
            manifest.info.depends = toml::find<std::vector<std::string>>(plugin, "depends");
        }

        // Optional: trust field
        if (plugin.contains("trust")) {
            auto trust_str = toml::find<std::string>(plugin, "trust");
            if (trust_str == "trusted") {
                manifest.trust = PluginTrust::Trusted;
            } else if (trust_str == "untrusted") {
                manifest.trust = PluginTrust::Untrusted;
            } else {
                return Error::config("Invalid trust value: " + trust_str +
                                     " (expected 'trusted' or 'untrusted')");
            }
        }

        // Optional: enabled field
        manifest.enabled = toml::find_or<bool>(plugin, "enabled", true);

        // Optional: [plugin.config] section
        if (plugin.contains("config")) {
            auto config_table = toml::find<toml::table>(plugin, "config");
            for (const auto& [k, v] : config_table) {
                manifest.config[k] = v.as_string();
            }
        }

    } catch (const toml::syntax_error& e) {
        return Error::parse(std::string("TOML syntax error in plugin.toml: ") + e.what());
    } catch (const std::out_of_range& e) {
        return Error::config(std::string("Missing required field in plugin.toml: ") + e.what());
    } catch (const std::exception& e) {
        return Error::config(std::string("Invalid field in plugin.toml: ") + e.what());
    }

    return manifest;
}

}  // namespace ea::plugin
