#include "PluginHost.h"
#include "log/Logger.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace ea::plugin {

PluginHost::PluginHost(tool::ToolRegistry* registry,
                       IMemory* memory,
                       security::SecurityPolicy* policy)
    : registry_(registry), memory_(memory), policy_(policy) {}

PluginHost::~PluginHost() {
    unload_all();
}

// ── Single plugin lifecycle ──────────────────────────────────────────────

Result<void> PluginHost::load(const PluginManifest& manifest) {
    const auto& name = manifest.info.name;
    if (plugins_.find(name) != plugins_.end()) {
        return Error::plugin("Plugin already loaded: " + name);
    }

    auto result = loader_.load(manifest);
    if (!result.ok()) {
        return result.error();
    }

    plugins_.emplace(name, std::move(result.value()));
    return {};
}

Result<void> PluginHost::initialize(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Error::plugin("Plugin not found: " + name);
    }
    auto& plugin = it->second;

    if (plugin.state != PluginState::Loaded) {
        return Error::plugin(
            "Plugin '" + name + "' not in Loaded state (current: " +
            std::to_string(static_cast<int>(plugin.state)) + ")");
    }

    PluginContext ctx = build_context(plugin);
    auto result = plugin.plugin_instance->on_init(ctx);
    if (result != PluginResult::Success) {
        plugin.state = PluginState::Error;
        return Error::plugin("Plugin initialization failed: " + name);
    }

    plugin.state = PluginState::Initialized;
    return {};
}

Result<void> PluginHost::activate(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Error::plugin("Plugin not found: " + name);
    }
    auto& plugin = it->second;

    if (plugin.state != PluginState::Initialized) {
        return Error::plugin(
            "Plugin '" + name + "' not in Initialized state (current: " +
            std::to_string(static_cast<int>(plugin.state)) + ")");
    }

    auto result = plugin.plugin_instance->on_activate();
    if (result != PluginResult::Success) {
        plugin.state = PluginState::Error;
        return Error::plugin("Plugin activation failed: " + name);
    }

    // Create shared_ptr<void> to the dl_handle with a NO-OP deleter.
    // Only PluginLoader::unload() should call dlclose.
    auto so_handle = std::shared_ptr<void>(plugin.dl_handle, [](void*){});

    PluginAdapters adapters;

    // Collect tools from plugin (always allowed)
    auto tools = plugin.plugin_instance->create_tools();
    for (auto& tool : tools) {
        auto adapter = std::make_unique<PluginToolAdapter>(
            std::move(tool), so_handle);
        if (registry_) {
            registry_->register_tool(std::move(adapter));
        } else {
            adapters.tools.push_back(std::move(adapter));
        }
    }

    // Collect providers (trusted only)
    if (plugin.manifest.trust == PluginTrust::Trusted) {
        auto providers = plugin.plugin_instance->create_providers();
        for (auto& provider : providers) {
            auto adapter = std::make_unique<PluginProviderAdapter>(
                std::move(provider), so_handle);
            adapters.providers.push_back(std::move(adapter));
        }
    } else if (!plugin.plugin_instance->create_providers().empty()) {
        EA_WARN("Untrusted plugin '{}' attempted to register providers — skipped", name);
    }

    // Collect steps (trusted only)
    if (plugin.manifest.trust == PluginTrust::Trusted) {
        auto steps = plugin.plugin_instance->create_steps();
        for (auto& step : steps) {
            auto adapter = std::make_unique<PluginStepAdapter>(
                std::move(step), so_handle);
            adapters.steps.push_back(std::move(adapter));
        }
    }

    // Collect listeners (trusted only)
    if (plugin.manifest.trust == PluginTrust::Trusted) {
        auto listeners = plugin.plugin_instance->create_listeners();
        for (auto& listener : listeners) {
            auto adapter = std::make_unique<PluginListenerAdapter>(
                std::move(listener), so_handle);
            adapters.listeners.push_back(std::move(adapter));
        }
    }

    adapters_.emplace(name, std::move(adapters));
    plugin.state = PluginState::Active;
    return {};
}

Result<void> PluginHost::deactivate(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Error::plugin("Plugin not found: " + name);
    }
    auto& plugin = it->second;

    if (plugin.state != PluginState::Active) {
        return Error::plugin(
            "Plugin '" + name + "' not in Active state (current: " +
            std::to_string(static_cast<int>(plugin.state)) + ")");
    }

    plugin.plugin_instance->on_deactivate();

    // Unregister adapters
    auto adapter_it = adapters_.find(name);
    if (adapter_it != adapters_.end()) {
        // Tools registered via registry_ need to be unregistered
        // (they were moved into the registry, so we deactivate the plugin's
        //  toolset. For simplicity, tools added via register_tool go to the
        //  "default" toolset, which is shared — we cannot remove individual
        //  tools from it with the current ToolRegistry API.
        // The adapter storage here is for providers/steps/listeners that
        // aren't registered with a global service.)

        // Clear adapter storage — the adapters will be destroyed,
        // which drops the so_handle shared_ptr refs.
        adapters_.erase(adapter_it);
    }

    plugin.state = PluginState::Initialized;
    return {};
}

Result<void> PluginHost::unload(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Error::plugin("Plugin not found: " + name);
    }
    auto& plugin = it->second;

    // Deactivate first if active
    if (plugin.state == PluginState::Active) {
        plugin.plugin_instance->on_deactivate();
        adapters_.erase(name);
        plugin.state = PluginState::Initialized;
    }

    // Call on_destroy before unloading
    if (plugin.plugin_instance) {
        plugin.plugin_instance->on_destroy();
    }

    // If the plugin was loaded via dlopen, use the loader to unload properly.
    // If it was injected (no dl_handle), delete the instance directly.
    if (plugin.dl_handle) {
        auto unload_result = loader_.unload(plugin);
        if (!unload_result.ok()) {
            return unload_result.error();
        }
    } else {
        // Injected plugin — delete instance directly
        delete plugin.plugin_instance;
        plugin.plugin_instance = nullptr;
        plugin.state = PluginState::Unloaded;
    }

    plugins_.erase(it);
    return {};
}

// ── Batch operations ─────────────────────────────────────────────────────

Result<void> PluginHost::load_all(const std::vector<PluginManifest>& manifests) {
    bool any_failed = false;
    for (const auto& manifest : manifests) {
        if (!manifest.enabled) continue;
        auto result = load(manifest);
        if (!result.ok()) {
            EA_ERROR("Failed to load plugin '{}': {}",
                     manifest.info.name, result.error().message);
            any_failed = true;
        }
    }
    if (any_failed) {
        return Error::plugin("One or more plugins failed to load");
    }
    return {};
}

Result<void> PluginHost::initialize_all() {
    bool any_failed = false;
    for (auto& [name, plugin] : plugins_) {
        if (plugin.state == PluginState::Loaded) {
            auto result = initialize(name);
            if (!result.ok()) {
                EA_ERROR("Failed to initialize plugin '{}': {}",
                         name, result.error().message);
                any_failed = true;
            }
        }
    }
    if (any_failed) {
        return Error::plugin("One or more plugins failed to initialize");
    }
    return {};
}

Result<void> PluginHost::activate_all() {
    bool any_failed = false;
    for (auto& [name, plugin] : plugins_) {
        if (plugin.state == PluginState::Initialized) {
            auto result = activate(name);
            if (!result.ok()) {
                EA_ERROR("Failed to activate plugin '{}': {}",
                         name, result.error().message);
                any_failed = true;
            }
        }
    }
    if (any_failed) {
        return Error::plugin("One or more plugins failed to activate");
    }
    return {};
}

void PluginHost::deactivate_all() {
    for (auto& [name, plugin] : plugins_) {
        if (plugin.state == PluginState::Active) {
            plugin.plugin_instance->on_deactivate();
            adapters_.erase(name);
            plugin.state = PluginState::Initialized;
        }
    }
}

void PluginHost::unload_all() {
    // Deactivate all first, then unload
    for (auto& [name, plugin] : plugins_) {
        if (plugin.state == PluginState::Active) {
            plugin.plugin_instance->on_deactivate();
            plugin.state = PluginState::Initialized;
        }
        if (plugin.plugin_instance) {
            plugin.plugin_instance->on_destroy();
        }
    }
    adapters_.clear();

    // Unload all plugins
    for (auto& [name, plugin] : plugins_) {
        if (plugin.dl_handle) {
            loader_.unload(plugin);
        } else {
            // Injected plugin — delete instance directly
            delete plugin.plugin_instance;
            plugin.plugin_instance = nullptr;
            plugin.state = PluginState::Unloaded;
        }
    }
    plugins_.clear();
}

// ── Query ────────────────────────────────────────────────────────────────

const LoadedPlugin* PluginHost::get(const std::string& name) const {
    auto it = plugins_.find(name);
    return it != plugins_.end() ? &it->second : nullptr;
}

std::vector<std::string> PluginHost::names() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : plugins_) {
        result.push_back(name);
    }
    return result;
}

PluginState PluginHost::state(const std::string& name) const {
    auto it = plugins_.find(name);
    return it != plugins_.end() ? it->second.state : PluginState::Unloaded;
}

void PluginHost::inject_plugin(const std::string& name, LoadedPlugin plugin) {
    plugins_.emplace(name, std::move(plugin));
}

// ── Private ──────────────────────────────────────────────────────────────

PluginContext PluginHost::build_context(const LoadedPlugin& plugin) {
    PluginContext ctx;

    // Logging (always available)
    ctx.log_info  = [](const char* msg) { EA_INFO("{}", msg); };
    ctx.log_warn  = [](const char* msg) { EA_WARN("{}", msg); };
    ctx.log_error = [](const char* msg) { EA_ERROR("{}", msg); };
    ctx.log_debug = [](const char* msg) { EA_DEBUG("{}", msg); };

    // ToolRegistry (always available)
    // We cast our ToolRegistry* to the opaque host::ToolRegistry*
    // This is safe because the host::ToolRegistry is a forward declaration
    // used only for null-checking in the plugin.
    ctx.tool_registry = reinterpret_cast<host::ToolRegistry*>(registry_);

    // Config from manifest
    ctx.config = plugin.manifest.config;
    ctx.plugin_dir = plugin.manifest.path;
    ctx.trust = plugin.manifest.trust;

    if (plugin.manifest.trust == PluginTrust::Trusted) {
        // Trusted plugins get access to all services
        ctx.provider_factory = reinterpret_cast<host::ProviderFactory*>(0x1);  // Non-null sentinel
        ctx.event_bus = reinterpret_cast<host::EventBus*>(0x1);  // Non-null sentinel
    }
    // Untrusted plugins: provider_factory and event_bus remain nullptr

    return ctx;
}

Result<std::vector<std::string>> PluginHost::resolve_load_order() const {
    // Topological sort using Kahn's algorithm
    // Build adjacency list: dep -> [plugins that depend on dep]
    std::unordered_map<std::string, std::vector<std::string>> graph;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& [name, plugin] : plugins_) {
        if (graph.find(name) == graph.end()) {
            graph[name] = {};
        }
        if (in_degree.find(name) == in_degree.end()) {
            in_degree[name] = 0;
        }
        for (const auto& dep : plugin.manifest.info.depends) {
            graph[dep].push_back(name);
            in_degree[name]++;
        }
    }

    // Seed with nodes that have no dependencies
    std::vector<std::string> order;
    std::vector<std::string> queue;
    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) {
            queue.push_back(name);
        }
    }

    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();
        order.push_back(current);

        for (const auto& neighbor : graph[current]) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) {
                queue.push_back(neighbor);
            }
        }
    }

    if (order.size() != plugins_.size()) {
        return Error::plugin("Circular dependency detected among plugins");
    }

    return order;
}

}  // namespace ea::plugin
