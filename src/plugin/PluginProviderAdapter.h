#pragma once
#include "core/IProvider.h"
#include <memory>

namespace ea::plugin {

// Adapter that wraps a plugin-created IProvider with crash protection.
// Holds a shared_ptr<void> to the dlopen handle so the .so stays loaded.
class PluginProviderAdapter : public IProvider {
public:
    PluginProviderAdapter(std::unique_ptr<IProvider> provider, std::shared_ptr<void> so_handle)
        : provider_(std::move(provider)), so_handle_(std::move(so_handle)) {}

    std::string name() const override {
        try { return provider_->name(); }
        catch (...) { return "<plugin-error>"; }
    }

    std::vector<std::string> list_models() const override {
        try { return provider_->list_models(); }
        catch (...) { return {}; }
    }

    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) override
    {
        try { return provider_->chat(messages, tools, model, opts); }
        catch (const std::exception& e) {
            return Error::plugin(std::string("Plugin exception: ") + e.what());
        }
        catch (...) {
            return Error::plugin("Plugin crashed with unknown exception");
        }
    }

    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) override
    {
        try { return provider_->stream_chat(messages, tools, model, std::move(on_chunk), opts); }
        catch (const std::exception& e) {
            return Error::plugin(std::string("Plugin exception: ") + e.what());
        }
        catch (...) {
            return Error::plugin("Plugin crashed with unknown exception");
        }
    }

    provider::ProviderCapabilities capabilities() const override {
        try { return provider_->capabilities(); }
        catch (...) { return {}; }  // All false — safest default
    }

private:
    std::unique_ptr<IProvider> provider_;
    std::shared_ptr<void> so_handle_;  // Keeps .so loaded
};

}  // namespace ea::plugin
