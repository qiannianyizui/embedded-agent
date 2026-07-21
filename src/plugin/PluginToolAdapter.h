#pragma once
#include "core/ITool.h"
#include <memory>

namespace ea::plugin {

// Adapter that wraps a plugin-created ITool with crash protection.
// Holds a shared_ptr<void> to the dlopen handle so the .so stays loaded
// as long as any adapter instance exists.
class PluginToolAdapter : public ITool {
public:
    PluginToolAdapter(std::unique_ptr<ITool> tool, std::shared_ptr<void> so_handle)
        : tool_(std::move(tool)), so_handle_(std::move(so_handle)) {}

    std::string name() const override {
        try { return tool_->name(); }
        catch (...) { return "<plugin-error>"; }
    }

    std::string description() const override {
        try { return tool_->description(); }
        catch (...) { return "<plugin-error>"; }
    }

    json parameters_schema() const override {
        try { return tool_->parameters_schema(); }
        catch (...) { return json::object(); }
    }

    Result<ToolResult> execute(const json& args) override {
        try { return tool_->execute(args); }
        catch (const std::exception& e) {
            return Error::plugin(std::string("Plugin exception: ") + e.what());
        }
        catch (...) {
            return Error::plugin("Plugin crashed with unknown exception");
        }
    }

    bool is_mutating() const override {
        try { return tool_->is_mutating(); }
        catch (...) { return true; }  // Assume mutating on error (safe default)
    }

    bool is_dangerous() const override {
        try { return tool_->is_dangerous(); }
        catch (...) { return true; }  // Assume dangerous on error (safe default)
    }

private:
    std::unique_ptr<ITool> tool_;
    std::shared_ptr<void> so_handle_;  // Keeps .so loaded via custom deleter (dlclose)
};

}  // namespace ea::plugin
