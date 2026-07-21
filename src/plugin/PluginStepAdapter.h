#pragma once
#include "agent/ITurnStep.h"
#include <memory>

namespace ea::plugin {

// Adapter that wraps a plugin-created ITurnStep with crash protection.
// Holds a shared_ptr<void> to the dlopen handle so the .so stays loaded.
class PluginStepAdapter : public agent::ITurnStep {
public:
    PluginStepAdapter(std::unique_ptr<agent::ITurnStep> step, std::shared_ptr<void> so_handle)
        : step_(std::move(step)), so_handle_(std::move(so_handle)) {}

    Result<void> execute(agent::TurnContext& ctx) override {
        try { return step_->execute(ctx); }
        catch (const std::exception& e) {
            return Error::plugin(std::string("Plugin exception: ") + e.what());
        }
        catch (...) {
            return Error::plugin("Plugin crashed with unknown exception");
        }
    }

    std::string name() const override {
        try { return step_->name(); }
        catch (...) { return "<plugin-error>"; }
    }

private:
    std::unique_ptr<agent::ITurnStep> step_;
    std::shared_ptr<void> so_handle_;  // Keeps .so loaded
};

}  // namespace ea::plugin
