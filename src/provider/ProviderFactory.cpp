#include "ProviderFactory.h"
#include "OpenAIProvider.h"
#include "AnthropicProvider.h"
#include "OllamaProvider.h"

namespace ea::provider {

std::unique_ptr<IProvider> create(const config::ProviderConfig& cfg) {
    if (cfg.type == "openai_compatible") {
        OpenAIProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.api_key = cfg.api_key;
        pcfg.default_model = cfg.default_model;
        pcfg.tls = cfg.tls;
        pcfg.retry = cfg.retry;
        pcfg.timeout = cfg.timeout;
        return std::make_unique<OpenAIProvider>(std::move(pcfg));
    } else if (cfg.type == "anthropic") {
        AnthropicProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.api_key = cfg.api_key;
        pcfg.default_model = cfg.default_model;
        pcfg.tls = cfg.tls;
        pcfg.retry = cfg.retry;
        pcfg.timeout = cfg.timeout;
        return std::make_unique<AnthropicProvider>(std::move(pcfg));
    } else if (cfg.type == "ollama") {
        OllamaProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.timeout = cfg.timeout;
        return std::make_unique<OllamaProvider>(std::move(pcfg));
    }
    return nullptr;
}

}  // namespace ea::provider
