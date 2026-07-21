#pragma once
#include "plugin/PluginApi.h"

namespace ea::plugin::mock {

class MockPlugin : public IPlugin {
public:
    PluginInfo info() const override {
        return PluginInfo{
            "mock_plugin",
            "1.0.0",
            "Mock plugin for testing PluginLoader",
            EA_PLUGIN_API_VERSION,
            {"tool"},
            {}
        };
    }

    PluginResult on_init(const PluginContext&) override {
        initialized_ = true;
        return PluginResult::Success;
    }

    PluginResult on_activate() override {
        active_ = true;
        return PluginResult::Success;
    }

    void on_deactivate() override {
        active_ = false;
    }

    void on_destroy() override {
        destroyed_ = true;
    }

    bool is_initialized() const { return initialized_; }
    bool is_active() const { return active_; }
    bool is_destroyed() const { return destroyed_; }

private:
    bool initialized_ = false;
    bool active_ = false;
    bool destroyed_ = false;
};

}  // namespace ea::plugin::mock
