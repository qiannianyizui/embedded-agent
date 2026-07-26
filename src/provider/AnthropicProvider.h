#pragma once
#include "provider/IProvider.h"
#include "common/net/HttpClient.h"

namespace ea::provider {

class AnthropicProvider : public IProvider {
public:
    struct Config {
        std::string base_url = "https://api.anthropic.com";
        std::string api_key;
        std::string default_model = "claude-sonnet-4-6";
        std::string api_version = "2023-06-01";
        net::TlsConfig tls;
        net::RetryPolicy retry;
        std::chrono::milliseconds timeout{60000};
    };

    explicit AnthropicProvider(Config config);

    std::string name() const override { return "anthropic"; }
    std::vector<std::string> list_models() const override;
    ProviderCapabilities capabilities() const override {
        return {true, true, true, true, true};
    }

    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}
    ) override;

    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}
    ) override;

    // Exposed for testing
    json build_request_body(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts,
        bool stream
    ) const;

    Result<LLMResponse> parse_response(const json& body) const;

private:
    Config config_;
    net::HttpClient client_;
};

}  // namespace ea::provider
