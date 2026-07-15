#include "ProviderFactory.h"
#include "OpenAIProvider.h"
#include "AnthropicProvider.h"
#include "OllamaProvider.h"
#include "ReliableProvider.h"

namespace ea::provider {

std::unique_ptr<IProvider> create(const config::ProviderConfig& cfg,
                                   const FactoryOptions& opts)
{
    if (cfg.type == "openai_compatible") {
        OpenAIProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.api_key = cfg.api_key;
        pcfg.default_model = cfg.default_model;
        pcfg.tls = cfg.tls;
        pcfg.retry = cfg.retry;
        pcfg.timeout = cfg.timeout;

        if (opts.enable_retry && opts.max_retries > 0) {
            auto primary = std::make_shared<OpenAIProvider>(std::move(pcfg));
            ReliableProvider::Config rcfg;
            rcfg.max_retries = opts.max_retries;
            return std::make_unique<ReliableProvider>(primary, rcfg);
        }
        return std::make_unique<OpenAIProvider>(std::move(pcfg));

    } else if (cfg.type == "anthropic") {
        AnthropicProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.api_key = cfg.api_key;
        pcfg.default_model = cfg.default_model;
        pcfg.tls = cfg.tls;
        pcfg.retry = cfg.retry;
        pcfg.timeout = cfg.timeout;

        if (opts.enable_retry && opts.max_retries > 0) {
            auto primary = std::make_shared<AnthropicProvider>(std::move(pcfg));
            ReliableProvider::Config rcfg;
            rcfg.max_retries = opts.max_retries;
            return std::make_unique<ReliableProvider>(primary, rcfg);
        }
        return std::make_unique<AnthropicProvider>(std::move(pcfg));

    } else if (cfg.type == "ollama") {
        OllamaProvider::Config pcfg;
        pcfg.base_url = cfg.base_url;
        pcfg.timeout = cfg.timeout;

        if (opts.enable_retry && opts.max_retries > 0) {
            auto primary = std::make_shared<OllamaProvider>(std::move(pcfg));
            ReliableProvider::Config rcfg;
            rcfg.max_retries = opts.max_retries;
            return std::make_unique<ReliableProvider>(primary, rcfg);
        }
        return std::make_unique<OllamaProvider>(std::move(pcfg));
    }

    return nullptr;
}

}  // namespace ea::provider
