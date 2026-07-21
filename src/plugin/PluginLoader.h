#pragma once
#include "PluginApi.h"
#include "PluginManifest.h"
#include "common/base/Result.h"
#include <string>
#include <map>

namespace ea::plugin {

// C-linkage entry points that every plugin .so must export
struct PluginEntryPoints {
    int (*api_version)() = nullptr;
    IPlugin* (*create_plugin)() = nullptr;
    void (*destroy_plugin)(IPlugin*) = nullptr;
};

struct LoadedPlugin {
    void* dl_handle = nullptr;
    PluginEntryPoints entry_points;
    PluginManifest manifest;
    PluginState state = PluginState::Unloaded;
    IPlugin* plugin_instance = nullptr;  // Owned by the loader; destroyed via destroy_plugin
};

class PluginLoader {
public:
    Result<LoadedPlugin> load(const PluginManifest& manifest);
    Result<void> unload(LoadedPlugin& plugin);

private:
    Result<void> resolve_symbols(void* handle, PluginEntryPoints& eps);
    Result<void> check_api_version(const PluginEntryPoints& eps);
};

}  // namespace ea::plugin
