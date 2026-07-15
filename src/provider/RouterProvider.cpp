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
    if (config_.default_provider) {
        return config_.default_provider->capabilities();
    }
    return {};
}

std::shared_ptr<IProvider> RouterProvider::resolve_provider(const ChatOptions& opts) const {
    if (opts.route_hint.has_value()) {
        for (const auto& rule : config_.rules) {
            if (rule.model_hint == opts.route_hint.value()) {
                return rule.provider;
            }
        }
    }
    return config_.default_provider;
}

Result<LLMResponse> RouterProvider::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts)
{
    auto provider = resolve_provider(opts);
    if (!provider) {
        return Error::net("No provider available for route");
    }

    std::string effective_model = model;
    if (effective_model.empty() && opts.route_hint.has_value()) {
        for (const auto& rule : config_.rules) {
            if (rule.model_hint == opts.route_hint.value() && !rule.model.empty()) {
                effective_model = rule.model;
                break;
            }
        }
    }

    return provider->chat(messages, tools, effective_model, opts);
}

Result<void> RouterProvider::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts)
{
    auto provider = resolve_provider(opts);
    if (!provider) {
        return Error::net("No provider available for route");
    }

    std::string effective_model = model;
    if (effective_model.empty() && opts.route_hint.has_value()) {
        for (const auto& rule : config_.rules) {
            if (rule.model_hint == opts.route_hint.value() && !rule.model.empty()) {
                effective_model = rule.model;
                break;
            }
        }
    }

    return provider->stream_chat(messages, tools, effective_model, on_chunk, opts);
}

}  // namespace ea::provider
