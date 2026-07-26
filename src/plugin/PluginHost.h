#pragma once
#include "PluginLoader.h"
#include "PluginApi.h"
#include "PluginToolAdapter.h"
#include "PluginProviderAdapter.h"
#include "PluginStepAdapter.h"
#include "PluginListenerAdapter.h"
#include "tool/ToolRegistry.h"
#include "memory/IMemory.h"
#include "security/SecurityPolicy.h"
#include "common/base/Result.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ea::plugin {

// PluginHost — top-level plugin lifecycle manager.
// Owns the PluginLoader and orchestrates loading, initialization,
// activation, and deactivation of plugins, connecting them to host services.
class PluginHost {
public:
    PluginHost(tool::ToolRegistry* registry,
               IMemory* memory,
               security::SecurityPolicy* policy);
    ~PluginHost();

    // ── Single plugin lifecycle ──────────────────────────────────────────
    Result<void> load(const PluginManifest& manifest);
    Result<void> initialize(const std::string& name);
    Result<void> activate(const std::string& name);
    Result<void> deactivate(const std::string& name);
    Result<void> unload(const std::string& name);

    // ── Batch operations (startup sequence) ──────────────────────────────
    // Failures are logged but don't stop other plugins.
    Result<void> load_all(const std::vector<PluginManifest>& manifests);
    Result<void> initialize_all();
    Result<void> activate_all();
    void deactivate_all();
    void unload_all();

    // ── Query ────────────────────────────────────────────────────────────
    const LoadedPlugin* get(const std::string& name) const;
    std::vector<std::string> names() const;
    PluginState state(const std::string& name) const;

    // ── Testing support ──────────────────────────────────────────────────
    // Directly insert a pre-loaded plugin (bypasses dlopen).
    // For use in unit tests only.
    void inject_plugin(const std::string& name, LoadedPlugin plugin);

private:
    // Build PluginContext for a plugin based on its trust level
    PluginContext build_context(const LoadedPlugin& plugin);

    // Resolve load order based on dependency graph (topological sort)
    // Returns ordered list of plugin names, or Error on cycle
    Result<std::vector<std::string>> resolve_load_order() const;

    PluginLoader loader_;
    std::map<std::string, LoadedPlugin> plugins_;

    // Host service references (non-owning)
    tool::ToolRegistry* registry_;
    IMemory* memory_;
    security::SecurityPolicy* policy_;

    // Adapters created during plugin activation (owned by PluginHost).
    // Keyed by plugin name so we can unregister on deactivation.
    struct PluginAdapters {
        std::vector<std::unique_ptr<PluginToolAdapter>> tools;
        std::vector<std::unique_ptr<PluginProviderAdapter>> providers;
        std::vector<std::unique_ptr<PluginStepAdapter>> steps;
        std::vector<std::unique_ptr<PluginListenerAdapter>> listeners;
    };
    std::map<std::string, PluginAdapters> adapters_;
};

}  // namespace ea::plugin
