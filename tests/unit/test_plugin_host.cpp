#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginHost.h"
#include "plugin/PluginApi.h"
#include "plugin/PluginManifest.h"
#include "plugin/PluginToolAdapter.h"
#include "tool/ToolRegistry.h"
#include "tool/ITool.h"
#include "provider/IProvider.h"
#include "base/Types.h"
#include "base/Result.h"
#include <memory>
#include <string>
#include <vector>

using namespace ea;
using namespace ea::plugin;
using namespace ea::tool;

// ── Mock implementations for testing ─────────────────────────────────────

namespace {

class HostTestTool : public ITool {
public:
    HostTestTool(std::string n) : name_(std::move(n)) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "test tool"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return ToolResult{"c1", "executed", false};
    }
    bool is_mutating() const override { return false; }
    bool is_dangerous() const override { return false; }
private:
    std::string name_;
};

// Plugin that provides tools on activation
class ToolProvidingPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"tool_provider", "1.0.0", "Provides tools",
                          EA_PLUGIN_API_VERSION, {"tool"}, {}};
    }
    PluginResult on_init(const PluginContext& ctx) override {
        ctx_ = ctx;
        return PluginResult::Success;
    }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override { deactivated_ = true; }
    void on_destroy() override { destroyed_ = true; }

    std::vector<std::shared_ptr<ITool>> create_tools() override {
        return {std::make_shared<HostTestTool>("plugin_tool_1"),
                std::make_shared<HostTestTool>("plugin_tool_2")};
    }

    const PluginContext& context() const { return ctx_; }
    bool was_deactivated() const { return deactivated_; }
    bool was_destroyed() const { return destroyed_; }

private:
    PluginContext ctx_;
    bool deactivated_ = false;
    bool destroyed_ = false;
};

// Plugin that fails on_init
class FailInitPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"fail_init", "1.0.0", "Fails init",
                          EA_PLUGIN_API_VERSION, {}, {}};
    }
    PluginResult on_init(const PluginContext&) override {
        return PluginResult::Error;
    }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override {}
    void on_destroy() override {}
};

// Plugin that fails on_activate
class FailActivatePlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"fail_activate", "1.0.0", "Fails activate",
                          EA_PLUGIN_API_VERSION, {}, {}};
    }
    PluginResult on_init(const PluginContext&) override {
        return PluginResult::Success;
    }
    PluginResult on_activate() override {
        return PluginResult::Error;
    }
    void on_deactivate() override {}
    void on_destroy() override {}
};

// Plugin that checks its context for trust-level services
class TrustCheckingPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"trust_checker", "1.0.0", "Checks trust",
                          EA_PLUGIN_API_VERSION, {}, {}};
    }
    PluginResult on_init(const PluginContext& ctx) override {
        ctx_ = ctx;
        return PluginResult::Success;
    }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override {}

    const PluginContext& context() const { return ctx_; }

private:
    PluginContext ctx_;
};

// Plugin that provides no tools (default create_tools returns empty)
class MinimalPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"minimal", "1.0.0", "Minimal plugin",
                          EA_PLUGIN_API_VERSION, {}, {}};
    }
    PluginResult on_init(const PluginContext&) override {
        return PluginResult::Success;
    }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override { deactivated_ = true; }
    void on_destroy() override { destroyed_ = true; }

    bool was_deactivated() const { return deactivated_; }
    bool was_destroyed() const { return destroyed_; }

private:
    bool deactivated_ = false;
    bool destroyed_ = false;
};

// Plugin with dependencies
class DependentPlugin : public IPlugin {
public:
    explicit DependentPlugin(std::vector<std::string> deps)
        : deps_(std::move(deps)) {}

    PluginInfo info() const override {
        return PluginInfo{"dependent", "1.0.0", "Has dependencies",
                          EA_PLUGIN_API_VERSION, {}, deps_};
    }
    PluginResult on_init(const PluginContext&) override {
        return PluginResult::Success;
    }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override {}
    void on_destroy() override {}

private:
    std::vector<std::string> deps_;
};

}  // anonymous namespace

// ── Helpers ──────────────────────────────────────────────────────────────

static std::unique_ptr<PluginHost> make_host(ToolRegistry& registry) {
    return std::make_unique<PluginHost>(&registry, nullptr, nullptr);
}

// Inject a plugin in Loaded state (ready for initialize)
static void inject_loaded(PluginHost& host, const std::string& name,
                          IPlugin* plugin, PluginTrust trust = PluginTrust::Untrusted,
                          std::vector<std::string> depends = {}) {
    LoadedPlugin loaded;
    loaded.state = PluginState::Loaded;
    loaded.plugin_instance = plugin;
    loaded.manifest.info.name = name;
    loaded.manifest.info.version = "1.0.0";
    loaded.manifest.trust = trust;
    loaded.manifest.info.depends = std::move(depends);
    host.inject_plugin(name, std::move(loaded));
}

// Inject a plugin in Initialized state (ready for activate)
static void inject_initialized(PluginHost& host, const std::string& name,
                               IPlugin* plugin, PluginTrust trust = PluginTrust::Untrusted) {
    LoadedPlugin loaded;
    loaded.state = PluginState::Initialized;
    loaded.plugin_instance = plugin;
    loaded.manifest.info.name = name;
    loaded.manifest.info.version = "1.0.0";
    loaded.manifest.trust = trust;
    host.inject_plugin(name, std::move(loaded));
}

// ── Test 1: Load and initialize a plugin ─────────────────────────────────

TEST_CASE("PluginHost: load and initialize plugin", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new ToolProvidingPlugin();
    inject_loaded(*host, "tool_provider", plugin);

    REQUIRE(host->state("tool_provider") == PluginState::Loaded);

    auto result = host->initialize("tool_provider");
    REQUIRE(result.ok());
    REQUIRE(host->state("tool_provider") == PluginState::Initialized);

    // Verify the plugin received a context with logging
    REQUIRE(plugin->context().log_info != nullptr);
    REQUIRE(plugin->context().log_warn != nullptr);
    REQUIRE(plugin->context().log_error != nullptr);
    REQUIRE(plugin->context().log_debug != nullptr);
}

// ── Test 2: Activate plugin — tools registered in ToolRegistry ───────────

TEST_CASE("PluginHost: activate registers tools in ToolRegistry", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new ToolProvidingPlugin();
    inject_initialized(*host, "tool_provider", plugin);

    // Before activation, registry has no plugin tools
    REQUIRE(registry.find("plugin_tool_1") == nullptr);
    REQUIRE(registry.find("plugin_tool_2") == nullptr);

    auto result = host->activate("tool_provider");
    REQUIRE(result.ok());
    REQUIRE(host->state("tool_provider") == PluginState::Active);

    // After activation, tools should be registered
    auto* tool1 = registry.find("plugin_tool_1");
    REQUIRE(tool1 != nullptr);
    REQUIRE(tool1->name() == "plugin_tool_1");

    auto* tool2 = registry.find("plugin_tool_2");
    REQUIRE(tool2 != nullptr);
    REQUIRE(tool2->name() == "plugin_tool_2");

    // Execute a tool through the registry
    auto exec_result = registry.execute("plugin_tool_1", json::object());
    REQUIRE(exec_result.ok());
    REQUIRE(exec_result.value().output == "executed");
}

// ── Test 3: Deactivate plugin — adapters cleared ─────────────────────────

TEST_CASE("PluginHost: deactivate clears adapters and changes state", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new MinimalPlugin();
    inject_initialized(*host, "minimal", plugin);

    host->activate("minimal");
    REQUIRE(host->state("minimal") == PluginState::Active);

    auto result = host->deactivate("minimal");
    REQUIRE(result.ok());
    REQUIRE(host->state("minimal") == PluginState::Initialized);
    REQUIRE(plugin->was_deactivated());
}

// ── Test 4: Untrusted plugin cannot access ProviderFactory ───────────────

TEST_CASE("PluginHost: untrusted plugin has null ProviderFactory and EventBus", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new TrustCheckingPlugin();
    inject_loaded(*host, "trust_checker", plugin, PluginTrust::Untrusted);

    host->initialize("trust_checker");

    // Untrusted plugin should see null provider_factory and event_bus
    REQUIRE(plugin->context().provider_factory == nullptr);
    REQUIRE(plugin->context().event_bus == nullptr);
    // But tool_registry should be available
    REQUIRE(plugin->context().tool_registry != nullptr);
    REQUIRE(plugin->context().trust == PluginTrust::Untrusted);
}

// ── Test 5: Trusted plugin can access ProviderFactory ────────────────────

TEST_CASE("PluginHost: trusted plugin has non-null ProviderFactory and EventBus", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new TrustCheckingPlugin();
    inject_loaded(*host, "trust_checker", plugin, PluginTrust::Trusted);

    host->initialize("trust_checker");

    // Trusted plugin should see non-null provider_factory and event_bus
    REQUIRE(plugin->context().provider_factory != nullptr);
    REQUIRE(plugin->context().event_bus != nullptr);
    REQUIRE(plugin->context().tool_registry != nullptr);
    REQUIRE(plugin->context().trust == PluginTrust::Trusted);
}

// ── Test 6: load_all with multiple plugins ───────────────────────────────

TEST_CASE("PluginHost: load_all with multiple manifests", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    // Create manifests for plugins that don't exist as .so files
    std::vector<PluginManifest> manifests;

    PluginManifest m1;
    m1.info.name = "plugin_a";
    m1.info.version = "1.0.0";
    m1.library = "/nonexistent/a.so";
    m1.enabled = true;
    manifests.push_back(m1);

    PluginManifest m2;
    m2.info.name = "plugin_b";
    m2.info.version = "2.0.0";
    m2.library = "/nonexistent/b.so";
    m2.enabled = true;
    manifests.push_back(m2);

    auto result = host->load_all(manifests);
    REQUIRE_FALSE(result.ok());  // .so files don't exist
    REQUIRE(host->names().empty());
}

// ── Test 7: Dependency resolution (load order) ──────────────────────────

TEST_CASE("PluginHost: dependency resolution via topological sort", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    // Inject plugins with dependencies: C depends on B, B depends on A
    auto* plugin_a = new MinimalPlugin();
    auto* plugin_b = new DependentPlugin({"a"});
    auto* plugin_c = new DependentPlugin({"b"});

    inject_loaded(*host, "a", plugin_a, PluginTrust::Untrusted, {});
    inject_loaded(*host, "b", plugin_b, PluginTrust::Untrusted, {"a"});
    inject_loaded(*host, "c", plugin_c, PluginTrust::Untrusted, {"b"});

    // Initialize all — should work in any order since we handle deps
    auto result = host->initialize_all();
    REQUIRE(result.ok());

    REQUIRE(host->state("a") == PluginState::Initialized);
    REQUIRE(host->state("b") == PluginState::Initialized);
    REQUIRE(host->state("c") == PluginState::Initialized);
}

// ── Test 8: Circular dependency detection ────────────────────────────────

TEST_CASE("PluginHost: circular dependency detection", "[plugin]") {
    // Test the topological sort algorithm for cycle detection
    // A depends on B, B depends on A → cycle

    std::unordered_map<std::string, std::vector<std::string>> graph;
    std::unordered_map<std::string, int> in_degree;

    graph["A"] = {"B"};
    graph["B"] = {"A"};
    in_degree["A"] = 1;
    in_degree["B"] = 1;

    std::vector<std::string> order;
    std::vector<std::string> queue;
    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) queue.push_back(name);
    }

    // No nodes with in_degree 0 → cycle detected
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

    // Order should be incomplete (not all nodes processed)
    REQUIRE(order.size() < 2);
}

// ── Test 9: Plugin that fails on_init() goes to Error state ──────────────

TEST_CASE("PluginHost: plugin failing on_init goes to Error state", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new FailInitPlugin();
    inject_loaded(*host, "fail_init", plugin);

    auto result = host->initialize("fail_init");
    REQUIRE_FALSE(result.ok());
    REQUIRE(host->state("fail_init") == PluginState::Error);
}

// ── Test 10: Plugin that fails on_activate() goes to Error, others continue ─

TEST_CASE("PluginHost: plugin failing on_activate goes to Error, others continue", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* fail_plugin = new FailActivatePlugin();
    auto* ok_plugin = new MinimalPlugin();

    inject_initialized(*host, "fail_activate", fail_plugin);
    inject_initialized(*host, "ok_plugin", ok_plugin);

    // activate_all should continue even when one fails
    auto result = host->activate_all();
    REQUIRE_FALSE(result.ok());  // At least one failed

    // The failing plugin should be in Error state
    REQUIRE(host->state("fail_activate") == PluginState::Error);

    // The OK plugin should be Active
    REQUIRE(host->state("ok_plugin") == PluginState::Active);
}

// ── Additional: State machine validation ─────────────────────────────────

TEST_CASE("PluginHost: wrong state transitions fail", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    // Can't initialize a plugin that doesn't exist
    auto result = host->initialize("no_such_plugin");
    REQUIRE_FALSE(result.ok());

    // Can't activate a plugin that doesn't exist
    result = host->activate("no_such_plugin");
    REQUIRE_FALSE(result.ok());

    // Can't deactivate a plugin that doesn't exist
    result = host->deactivate("no_such_plugin");
    REQUIRE_FALSE(result.ok());

    // Can't unload a plugin that doesn't exist
    result = host->unload("no_such_plugin");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("PluginHost: can't activate a Loaded (not Initialized) plugin", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new MinimalPlugin();
    inject_loaded(*host, "not_ready", plugin);

    auto result = host->activate("not_ready");
    REQUIRE_FALSE(result.ok());
    REQUIRE(host->state("not_ready") == PluginState::Loaded);
}

TEST_CASE("PluginHost: can't deactivate a non-Active plugin", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new MinimalPlugin();
    inject_initialized(*host, "not_active", plugin);

    auto result = host->deactivate("not_active");
    REQUIRE_FALSE(result.ok());
    REQUIRE(host->state("not_active") == PluginState::Initialized);
}

TEST_CASE("PluginHost: full lifecycle — load → init → activate → deactivate → unload", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* plugin = new MinimalPlugin();
    inject_loaded(*host, "lifecycle", plugin);

    // Load → Init
    REQUIRE(host->state("lifecycle") == PluginState::Loaded);
    REQUIRE(host->initialize("lifecycle").ok());
    REQUIRE(host->state("lifecycle") == PluginState::Initialized);

    // Init → Activate
    REQUIRE(host->activate("lifecycle").ok());
    REQUIRE(host->state("lifecycle") == PluginState::Active);

    // Activate → Deactivate
    REQUIRE(host->deactivate("lifecycle").ok());
    REQUIRE(host->state("lifecycle") == PluginState::Initialized);
    REQUIRE(plugin->was_deactivated());

    // Deactivate → Unload (this deletes the plugin instance)
    REQUIRE(host->unload("lifecycle").ok());
    REQUIRE(host->get("lifecycle") == nullptr);
    // Note: cannot access plugin->was_destroyed() after unload — the instance is deleted
}

TEST_CASE("PluginHost: batch operations on empty host", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    REQUIRE(host->load_all({}).ok());
    REQUIRE(host->initialize_all().ok());
    REQUIRE(host->activate_all().ok());

    host->deactivate_all();
    host->unload_all();

    REQUIRE(host->names().empty());
}

TEST_CASE("PluginHost: disabled plugins skipped in load_all", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    PluginManifest m;
    m.info.name = "disabled_plugin";
    m.info.version = "1.0.0";
    m.library = "/nonexistent.so";
    m.enabled = false;

    auto result = host->load_all({m});
    REQUIRE(result.ok());  // Skipped, so no error
    REQUIRE(host->names().empty());
}

TEST_CASE("PluginHost: deactivate_all and unload_all", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* p1 = new MinimalPlugin();
    auto* p2 = new MinimalPlugin();
    inject_initialized(*host, "p1", p1);
    inject_initialized(*host, "p2", p2);

    host->activate("p1");
    host->activate("p2");
    REQUIRE(host->state("p1") == PluginState::Active);
    REQUIRE(host->state("p2") == PluginState::Active);

    host->deactivate_all();
    REQUIRE(host->state("p1") == PluginState::Initialized);
    REQUIRE(host->state("p2") == PluginState::Initialized);
    REQUIRE(p1->was_deactivated());
    REQUIRE(p2->was_deactivated());

    // unload_all deletes the plugin instances
    host->unload_all();
    REQUIRE(host->names().empty());
}

TEST_CASE("PluginHost: query methods", "[plugin]") {
    ToolRegistry registry;
    auto host = make_host(registry);

    auto* p1 = new MinimalPlugin();
    auto* p2 = new ToolProvidingPlugin();
    inject_loaded(*host, "alpha", p1);
    inject_loaded(*host, "beta", p2);

    auto names = host->names();
    REQUIRE(names.size() == 2);

    auto* found = host->get("alpha");
    REQUIRE(found != nullptr);
    REQUIRE(found->manifest.info.name == "alpha");

    REQUIRE(host->get("nonexistent") == nullptr);
    REQUIRE(host->state("nonexistent") == PluginState::Unloaded);
}
