#pragma once
#include "agent/IEventListener.h"
#include <memory>

namespace ea::plugin {

// Adapter that wraps a plugin-created IEventListener with crash protection.
// Holds a shared_ptr<void> to the dlopen handle so the .so stays loaded.
class PluginListenerAdapter : public agent::IEventListener {
public:
    PluginListenerAdapter(std::shared_ptr<agent::IEventListener> listener, std::shared_ptr<void> so_handle)
        : listener_(std::move(listener)), so_handle_(std::move(so_handle)) {}

    void on_event(const agent::AgentEvent& event) override {
        try { listener_->on_event(event); }
        catch (...) {
            // Swallow — event listeners must not crash the host.
            // Logging could be added here if a logger reference is available.
        }
    }

private:
    std::shared_ptr<agent::IEventListener> listener_;
    std::shared_ptr<void> so_handle_;  // Keeps .so loaded
};

}  // namespace ea::plugin
