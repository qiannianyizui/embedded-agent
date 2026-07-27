#pragma once
#include "provider/IProvider.h"
#include "net/RetryPolicy.h"
#include <memory>
#include <vector>

namespace ea::provider {

class ReliableProvider : public IProvider {
public:
    struct Config {
        int max_retries = 3;
        std::chrono::milliseconds base_delay{1000};
        double backoff_multiplier = 2.0;
        std::vector<std::shared_ptr<IProvider>> fallbacks;
    };

    ReliableProvider(std::shared_ptr<IProvider> primary, Config config);

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
    Result<LLMResponse> try_with_retries(
        std::shared_ptr<IProvider> provider,
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts);

    std::shared_ptr<IProvider> primary_;
    Config config_;
};

}  // namespace ea::provider
