#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginToolAdapter.h"
#include "tool/ITool.h"
#include "common/base/Types.h"
#include "common/base/Result.h"
#include <memory>
#include <atomic>

using namespace ea;
using namespace ea::plugin;

// ── Mock tools for testing (anonymous namespace to avoid ODR issues) ────────
namespace {

class PluginStubTool : public ITool {
public:
    PluginStubTool(std::string n, std::string desc = "stub", bool mutating = true, bool dangerous = false)
        : name_(std::move(n)), desc_(std::move(desc)), mutating_(mutating), dangerous_(dangerous) {}

    std::string name() const override { return name_; }
    std::string description() const override { return desc_; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return ToolResult{"call_1", "ok", false};
    }
    bool is_mutating() const override { return mutating_; }
    bool is_dangerous() const override { return dangerous_; }

private:
    std::string name_;
    std::string desc_;
    bool mutating_;
    bool dangerous_;
};

class PluginErrorTool : public ITool {
public:
    std::string name() const override { return "error_tool"; }
    std::string description() const override { return "always errors"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return Error::tool_error("tool failed");
    }
    bool is_mutating() const override { return false; }
    bool is_dangerous() const override { return false; }
};

class PluginThrowingTool : public ITool {
public:
    std::string name() const override { return "throwing_tool"; }
    std::string description() const override { return "throws on execute"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        throw std::runtime_error("plugin blew up");
    }
    bool is_mutating() const override { return false; }
    bool is_dangerous() const override { return false; }
};

class PluginUnknownThrowTool : public ITool {
public:
    std::string name() const override { return "unknown_throw_tool"; }
    std::string description() const override { return "throws unknown"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        throw 42;  // Not a std::exception
    }
    bool is_mutating() const override { return false; }
    bool is_dangerous() const override { return false; }
};

// Fake so_handle: uses a shared counter so we can verify the handle outlives
// the adapter or is shared correctly.
struct HandleCounter {
    std::atomic<int> refcount{0};
    HandleCounter() { refcount = 1; }
};

struct HandleCounterDeleter {
    void operator()(void* p) const {
        delete static_cast<HandleCounter*>(p);
    }
};

static std::shared_ptr<void> make_fake_handle() {
    return std::shared_ptr<void>(new HandleCounter(), HandleCounterDeleter());
}

}  // anonymous namespace

// ── Tests ───────────────────────────────────────────────────────────────────

TEST_CASE("PluginToolAdapter delegates name()", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("my_tool"), make_fake_handle());
    REQUIRE(adapter->name() == "my_tool");
}

TEST_CASE("PluginToolAdapter delegates description()", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t", "a fine tool"), make_fake_handle());
    REQUIRE(adapter->description() == "a fine tool");
}

TEST_CASE("PluginToolAdapter delegates parameters_schema()", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t"), make_fake_handle());
    REQUIRE(adapter->parameters_schema() == json::object());
}

TEST_CASE("PluginToolAdapter delegates execute() — success", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t"), make_fake_handle());
    auto result = adapter->execute(json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "ok");
}

TEST_CASE("PluginToolAdapter delegates execute() — tool error", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginErrorTool>(), make_fake_handle());
    auto result = adapter->execute(json::object());
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::ToolError);
}

TEST_CASE("PluginToolAdapter delegates execute() — std::exception caught", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginThrowingTool>(), make_fake_handle());
    auto result = adapter->execute(json::object());
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::PluginError);
    REQUIRE(result.error().message.find("plugin blew up") != std::string::npos);
}

TEST_CASE("PluginToolAdapter delegates execute() — unknown exception caught", "[plugin]") {
    auto adapter = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginUnknownThrowTool>(), make_fake_handle());
    auto result = adapter->execute(json::object());
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::PluginError);
    REQUIRE(result.error().message.find("unknown exception") != std::string::npos);
}

TEST_CASE("PluginToolAdapter delegates is_mutating()", "[plugin]") {
    auto mutating = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t", "d", true, false), make_fake_handle());
    REQUIRE(mutating->is_mutating() == true);

    auto non_mutating = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t", "d", false, false), make_fake_handle());
    REQUIRE(non_mutating->is_mutating() == false);
}

TEST_CASE("PluginToolAdapter delegates is_dangerous()", "[plugin]") {
    auto safe = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t", "d", true, false), make_fake_handle());
    REQUIRE(safe->is_dangerous() == false);

    auto dangerous = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("t", "d", true, true), make_fake_handle());
    REQUIRE(dangerous->is_dangerous() == true);
}

TEST_CASE("PluginToolAdapter holds so_handle alive", "[plugin]") {
    // Verify that multiple adapters share the same so_handle and the handle
    // survives after the first adapter is destroyed.
    auto handle = make_fake_handle();

    // Create two adapters sharing the same handle
    auto adapter1 = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("a1"), handle);
    auto adapter2 = std::make_unique<PluginToolAdapter>(
        std::make_unique<PluginStubTool>("a2"), handle);

    // The shared_ptr refcount should have increased for each copy
    REQUIRE(handle.use_count() >= 3);  // original + two adapters

    // Destroy adapter1 — handle should still be valid
    adapter1.reset();
    REQUIRE(handle.use_count() >= 2);

    // adapter2 still works
    REQUIRE(adapter2->name() == "a2");
}

TEST_CASE("PluginToolAdapter destroys tool on deletion", "[plugin]") {
    // Track whether the inner tool was destroyed
    struct TrackingTool : public ITool {
        bool& destroyed_;
        TrackingTool(bool& flag) : destroyed_(flag) {}
        ~TrackingTool() override { destroyed_ = true; }
        std::string name() const override { return "track"; }
        std::string description() const override { return "track"; }
        json parameters_schema() const override { return json::object(); }
        Result<ToolResult> execute(const json&) override {
            return ToolResult{"c", "ok", false};
        }
        bool is_mutating() const override { return false; }
        bool is_dangerous() const override { return false; }
    };

    bool destroyed = false;
    {
        auto adapter = std::make_unique<PluginToolAdapter>(
            std::make_unique<TrackingTool>(destroyed), make_fake_handle());
        REQUIRE_FALSE(destroyed);
    }
    // After adapter is destroyed, the inner tool should also be destroyed
    REQUIRE(destroyed);
}
