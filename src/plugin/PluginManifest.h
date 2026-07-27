#pragma once
#include "PluginApi.h"
#include "base/Result.h"
#include <string>
#include <map>

namespace ea::plugin {

struct PluginManifest {
    PluginInfo info;
    std::string library;          // Relative path to .so (e.g., "libea_plugin_weather.so")
    PluginTrust trust = PluginTrust::Untrusted;
    bool enabled = true;
    std::string path;             // Absolute path to plugin directory
    std::map<std::string, std::string> config;
};

Result<PluginManifest> parse_manifest(const std::string& dir_path);

}  // namespace ea::plugin
