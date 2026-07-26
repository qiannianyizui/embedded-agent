// tests/security/test_plugin_sandbox.cpp
#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginToolAdapter.h"
#include "plugin/PluginProviderAdapter.h"
#include "plugin/PluginStepAdapter.h"
#include "plugin/PluginListenerAdapter.h"
#include "plugin/PluginHost.h"
#include "plugin/PluginApi.h"
#include "tool/ITool.h"
#include "provider/IProvider.h"
#include "agent/IEventListener.h"
#include "agent/AgentEvent.h"

using namespace ea;
using namespace ea::plugin;

namespace {

class ThrowingTool : public ITool {
public:
    std::string name() const override { throw std::runtime_error("crash"); }
    std::string description() const override { throw std::runtime_error("crash"); }
    json parameters_schema() const override { throw std::runtime_error("crash"); }
    Result<ToolResult> execute(const json&) override { throw std::runtime_error("crash"); }
    bool is_mutating() const override { throw std::runtime_error("crash"); }
    bool is_dangerous() const override { throw std::runtime_error("crash"); }
};

class ThrowingProvider : public IProvider {
public:
    std::string name() const override { throw std::runtime_error("crash"); }
    std::vector<std::string> list_models() const override { throw std::runtime_error("crash"); }
    provider::ProviderCapabilities capabilities() const override { throw std::runtime_error("crash"); }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        throw std::runtime_error("crash");
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        throw std::runtime_error("crash");
    }
};

class ThrowingListener : public agent::IEventListener {
public:
    void on_event(const agent::AgentEvent&) override { throw std::runtime_error("crash"); }
};

}  // anonymous namespace

TEST_CASE("Plugin sandbox: PluginToolAdapter catches all exceptions", "[security][plugin]") {
    auto throwing = std::make_shared<ThrowingTool>();
    auto so_handle = std::shared_ptr<void>(nullptr, [](void*){});
    PluginToolAdapter adapter(throwing, so_handle);

    REQUIRE(adapter.name() == "<plugin-error>");
    REQUIRE(adapter.description() == "<plugin-error>");
    REQUIRE(adapter.parameters_schema() == json::object());
    auto result = adapter.execute(json::object());
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::PluginError);
    REQUIRE(adapter.is_mutating() == true);   // safe default
    REQUIRE(adapter.is_dangerous() == true);  // safe default
}

TEST_CASE("Plugin sandbox: PluginProviderAdapter catches all exceptions", "[security][plugin]") {
    auto throwing = std::make_shared<ThrowingProvider>();
    auto so_handle = std::shared_ptr<void>(nullptr, [](void*){});
    PluginProviderAdapter adapter(throwing, so_handle);

    REQUIRE(adapter.name() == "<plugin-error>");
    REQUIRE(adapter.list_models().empty());
    auto caps = adapter.capabilities();
    // KNOWN BUG: PluginProviderAdapter.h comment says "All false — safest default",
    // but ProviderCapabilities struct defaults to streaming=true and
    // native_tool_calling=true. When a plugin crashes, the adapter returns
    // ProviderCapabilities{} which has these unsafe defaults, falsely
    // claiming the crashed plugin supports streaming and native tool calling.
    // The safe default should be all-false. These assertions document the
    // EXPECTED safe behavior; they will FAIL until PluginProviderAdapter is
    // fixed to return an explicit all-false ProviderCapabilities.
    //
    // Current (buggy) behavior:
    //   caps.streaming == true, caps.native_tool_calling == true
    // Expected (safe) behavior:
    //   caps.streaming == false, caps.native_tool_calling == false
    CHECK_FALSE(caps.streaming);            // TODO: fix PluginProviderAdapter
    CHECK_FALSE(caps.native_tool_calling);  // TODO: fix PluginProviderAdapter
    CHECK_FALSE(caps.vision);
    auto chat_result = adapter.chat({}, {}, "", {});
    REQUIRE_FALSE(chat_result.ok());
    REQUIRE(chat_result.error().code == ErrorCode::PluginError);
}

TEST_CASE("Plugin sandbox: PluginListenerAdapter swallows exceptions", "[security][plugin]") {
    auto throwing = std::make_shared<ThrowingListener>();
    auto so_handle = std::shared_ptr<void>(nullptr, [](void*){});
    PluginListenerAdapter adapter(throwing, so_handle);

    agent::AgentEvent event;
    // Must not crash
    REQUIRE_NOTHROW(adapter.on_event(event));
}

TEST_CASE("Plugin sandbox: Untrusted plugin context has null host services", "[security][plugin]") {
    PluginContext ctx;
    ctx.trust = PluginTrust::Untrusted;
    // Untrusted plugins should not have host service access
    REQUIRE(ctx.tool_registry == nullptr);
    REQUIRE(ctx.provider_factory == nullptr);
    REQUIRE(ctx.event_bus == nullptr);
}

TEST_CASE("Plugin sandbox: Trusted plugin context can have host services", "[security][plugin]") {
    PluginContext ctx;
    ctx.trust = PluginTrust::Trusted;
    // Trusted plugins can have host service access (set by PluginHost)
    REQUIRE(ctx.trust == PluginTrust::Trusted);
}
