#include "RouterProvider.h"

namespace ea::provider {

RouterProvider::RouterProvider(Config config)
    : config_(std::move(config)) {}

std::string RouterProvider::name() const { return "router"; }

std::vector<std::string> RouterProvider::list_models() const {
    std::vector<std::string> models;
    if (config_.default_provider) {
        auto default_models = config_.default_provider->list_models();
        models.insert(models.end(), default_models.begin(), default_models.end());
    }
    for (const auto& rule : config_.rules) {
        if (rule.provider) {
            auto rule_models = rule.provider->list_models();
            models.insert(models.end(), rule_models.begin(), rule_models.end());
        }
    }
    return models;
}

provider::ProviderCapabilities RouterProvider::capabilities() const {
    // Merge capabilities from all routed providers (union)
    provider::ProviderCapabilities result;
    auto merge = [&](const std::shared_ptr<IProvider>& p) {
        if (!p) return;
        auto c = p->capabilities();
        result.native_tool_calling |= c.native_tool_calling;
        result.streaming |= c.streaming;
        result.vision |= c.vision;
        result.prompt_caching |= c.prompt_caching;
        result.extended_thinking |= c.extended_thinking;
    };
    merge(config_.default_provider);
    for (const auto& rule : config_.rules) {
        merge(rule.provider);
    }
    return result;
}

ResolvedRoute RouterProvider::resolve(const ChatOptions& opts) const {
    ResolvedRoute route;
    route.provider = config_.default_provider;
    route.model = {};  // no override

    if (opts.route_hint.has_value()) {
        for (const auto& rule : config_.rules) {
            if (rule.model_hint == opts.route_hint.value()) {
                route.provider = rule.provider;
                if (!rule.model.empty()) {
                    route.model = rule.model;
                }
                return route;
            }
        }
    }
    return route;
}

Result<LLMResponse> RouterProvider::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts)
{
    auto route = resolve(opts);
    if (!route.provider) {
        return Error::net("No provider available for route");
    }

    std::string effective_model = model.empty() ? route.model : model;
    return route.provider->chat(messages, tools, effective_model, opts);
}

Result<void> RouterProvider::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts)
{
    auto route = resolve(opts);
    if (!route.provider) {
        return Error::net("No provider available for route");
    }

    std::string effective_model = model.empty() ? route.model : model;
    return route.provider->stream_chat(messages, tools, effective_model, on_chunk, opts);
}

}  // namespace ea::provider
