#pragma once
#include <string>
#include <vector>
#include <map>

#define EA_PLUGIN_API_VERSION 1

namespace ea::plugin {

enum class PluginState {
    Unloaded,       // Not loaded
    Loaded,         // dlopen succeeded, symbols resolved
    Initialized,    // on_init() succeeded
    Active,         // on_activate() succeeded
    Error           // In error state
};

enum class PluginResult {
    Success = 0,
    Error = 1,
    ApiVersionMismatch = 2,
    MissingDependency = 3,
    AlreadyInitialized = 4,
    NotInitialized = 5,
    AlreadyActive = 6,
    NotActive = 7
};

enum class PluginTrust {
    Untrusted,  // Can only register tools + access logging
    Trusted     // Full API access: tools, providers, steps, listeners, memory read
};

struct PluginInfo {
    std::string name;           // Unique identifier (alphanumeric + underscore)
    std::string version;        // Semver string "1.0.0"
    std::string description;    // Human-readable description
    int api_version = EA_PLUGIN_API_VERSION;
    std::vector<std::string> provides;  // "tool", "provider", "turn_step", "event_listener"
    std::vector<std::string> depends;   // Names of other plugins
};

// Opaque host service handles (forward declarations)
// Plugins never call methods on these directly; they are passed through
// so plugins can check for null (trust-level enforcement). Actual registration
// happens via IPlugin lifecycle methods where the host calls back.
namespace host {
    class ToolRegistry;
    class ProviderFactory;
    class EventBus;
}

// Context passed to plugin during initialization
struct PluginContext {
    // Logging (always available)
    void (*log_info)(const char* msg);
    void (*log_warn)(const char* msg);
    void (*log_error)(const char* msg);
    void (*log_debug)(const char* msg);

    // Host services (null if not allowed by trust level)
    host::ToolRegistry* tool_registry = nullptr;
    host::ProviderFactory* provider_factory = nullptr;
    host::EventBus* event_bus = nullptr;

    // Plugin-specific config from TOML
    std::map<std::string, std::string> config;

    // Plugin's own directory (for loading resources)
    std::string plugin_dir;

    // Trust level granted
    PluginTrust trust = PluginTrust::Untrusted;
};

// Core plugin interface — plugin authors implement this
class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Metadata
    virtual PluginInfo info() const = 0;

    // Lifecycle
    virtual PluginResult on_init(const PluginContext& ctx) = 0;
    virtual PluginResult on_activate() = 0;
    virtual void on_deactivate() = 0;
    virtual void on_destroy() {}
};

}  // namespace ea::plugin
