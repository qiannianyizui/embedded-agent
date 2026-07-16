#pragma once
#include "core/IProvider.h"
#include <memory>
#include <vector>

namespace ea::provider {

struct RouteRule {
    std::string model_hint;     // "code", "fast", "vision", "cheap"
    std::string provider_type;
    std::string model;
    std::shared_ptr<IProvider> provider;
};

struct ResolvedRoute {
    std::shared_ptr<IProvider> provider;
    std::string model;  // effective model override (empty = use default)
};

class RouterProvider : public IProvider {
public:
    struct Config {
        std::shared_ptr<IProvider> default_provider;
        std::vector<RouteRule> rules;
    };

    explicit RouterProvider(Config config);

    std::string name() const override;
    std::vector<std::string> list_models() const override;
    provider::ProviderCapabilities capabilities() const override;

    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) override;

    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) override;

private:
    ResolvedRoute resolve(const ChatOptions& opts) const;

    Config config_;
};

}  // namespace ea::provider
