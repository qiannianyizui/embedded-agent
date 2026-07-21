#include "PluginLoader.h"
#include <dlfcn.h>

namespace ea::plugin {

Result<void> PluginLoader::resolve_symbols(void* handle, PluginEntryPoints& eps) {
    eps.api_version = reinterpret_cast<int(*)()>(dlsym(handle, "ea_plugin_api_version"));
    if (!eps.api_version) {
        return Error::plugin("Missing symbol 'ea_plugin_api_version' in plugin");
    }

    eps.create_plugin = reinterpret_cast<IPlugin*(*)()>(dlsym(handle, "ea_plugin_create"));
    if (!eps.create_plugin) {
        return Error::plugin("Missing symbol 'ea_plugin_create' in plugin");
    }

    eps.destroy_plugin = reinterpret_cast<void(*)(IPlugin*)>(dlsym(handle, "ea_plugin_destroy"));
    if (!eps.destroy_plugin) {
        return Error::plugin("Missing symbol 'ea_plugin_destroy' in plugin");
    }

    return {};
}

Result<void> PluginLoader::check_api_version(const PluginEntryPoints& eps) {
    int version = eps.api_version();
    if (version != EA_PLUGIN_API_VERSION) {
        return Error::plugin(
            "API version mismatch: expected " + std::to_string(EA_PLUGIN_API_VERSION) +
            ", got " + std::to_string(version));
    }
    return {};
}

Result<LoadedPlugin> PluginLoader::load(const PluginManifest& manifest) {
    // Build the full path to the .so
    std::string so_path;
    if (!manifest.path.empty()) {
        so_path = manifest.path + "/" + manifest.library;
    } else {
        so_path = manifest.library;
    }

    // Open the shared library
    void* handle = dlopen(so_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        return Error::plugin(
            "Failed to load plugin library '" + so_path + "': " + dlerror());
    }

    LoadedPlugin loaded;
    loaded.dl_handle = handle;

    // Resolve the three required C-linkage entry points
    auto resolve_result = resolve_symbols(handle, loaded.entry_points);
    if (!resolve_result.ok()) {
        dlclose(handle);
        return resolve_result.error();
    }

    // Check API version compatibility
    auto version_result = check_api_version(loaded.entry_points);
    if (!version_result.ok()) {
        dlclose(handle);
        return version_result.error();
    }

    // Create the plugin instance
    loaded.plugin_instance = loaded.entry_points.create_plugin();
    if (!loaded.plugin_instance) {
        dlclose(handle);
        return Error::plugin("ea_plugin_create() returned null for plugin '" +
                              manifest.info.name + "'");
    }

    loaded.manifest = manifest;
    loaded.state = PluginState::Loaded;

    return loaded;
}

Result<void> PluginLoader::unload(LoadedPlugin& plugin) {
    if (!plugin.dl_handle) {
        // Already unloaded — no-op
        return {};
    }

    // Destroy the plugin instance via the plugin's own destroy function
    if (plugin.plugin_instance && plugin.entry_points.destroy_plugin) {
        plugin.entry_points.destroy_plugin(plugin.plugin_instance);
        plugin.plugin_instance = nullptr;
    }

    // Close the shared library
    dlclose(plugin.dl_handle);
    plugin.dl_handle = nullptr;
    plugin.state = PluginState::Unloaded;

    return {};
}

}  // namespace ea::plugin
