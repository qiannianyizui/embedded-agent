// tests/integration/test_plugin_integration.cpp
// Integration: plugin full lifecycle with ToolRegistry and AgentLoop
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "plugin/PluginHost.h"
#include "plugin/PluginApi.h"
#include "plugin/PluginManifest.h"
#include "tool/ToolRegistry.h"
#include "agent/AgentLoop.h"

using namespace ea;
using namespace ea::test;
using namespace ea::plugin;
using namespace ea::agent;
using namespace ea::tool;

namespace {

class IntegrationPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{"integration_plugin", "1.0.0", "Test plugin",
                          EA_PLUGIN_API_VERSION, {"tool"}, {}};
    }
    PluginResult on_init(const PluginContext&) override { return PluginResult::Success; }
    PluginResult on_activate() override { return PluginResult::Success; }
    void on_deactivate() override {}
    void on_destroy() override {}

    std::vector<std::shared_ptr<ITool>> create_tools() override {
        auto tool = std::make_shared<MockTool>("plugin_tool", "plugin result", false, false);
        return {tool};
    }
};

}  // anonymous namespace

TEST_CASE("Integration: plugin full lifecycle with ToolRegistry", "[integration][plugin]") {
    ToolRegistry registry;
    security::SecurityPolicy policy(security::AutonomyLevel::Supervised);
    PluginHost host(&registry, nullptr, &policy);

    // Inject plugin (bypass dlopen)
    LoadedPlugin lp;
    lp.plugin_instance = new IntegrationPlugin();
    lp.state = PluginState::Loaded;
    lp.manifest.info = PluginInfo{"integration_plugin", "1.0.0", "Test",
                                   EA_PLUGIN_API_VERSION, {"tool"}, {}};
    lp.manifest.trust = PluginTrust::Trusted;
    lp.manifest.enabled = true;
    host.inject_plugin("integration_plugin", std::move(lp));

    // Initialize
    auto init_result = host.initialize("integration_plugin");
    REQUIRE(init_result.ok());
    REQUIRE(host.state("integration_plugin") == PluginState::Initialized);

    // Activate — tools should be registered in ToolRegistry
    auto activate_result = host.activate("integration_plugin");
    REQUIRE(activate_result.ok());
    REQUIRE(host.state("integration_plugin") == PluginState::Active);

    // Use plugin tool through AgentLoop
    MockProvider provider;
    provider.enqueue(make_tools({make_call("plugin_tool", "c1")}));
    provider.enqueue_text("Used plugin tool");

    std::string output;
    AgentLoop loop(&provider, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });
    auto result = loop.run("use plugin tool");
    REQUIRE(result.ok());
    REQUIRE(output == "Used plugin tool");

    // Deactivate
    auto deactivate_result = host.deactivate("integration_plugin");
    REQUIRE(deactivate_result.ok());
    REQUIRE(host.state("integration_plugin") == PluginState::Initialized);

    // Unload
    auto unload_result = host.unload("integration_plugin");
    REQUIRE(unload_result.ok());
}
