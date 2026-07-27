#pragma once
#include "provider/IProvider.h"
#include "net/HttpClient.h"

namespace ea::provider {

class OllamaProvider : public IProvider {
public:
    struct Config {
        std::string base_url = "http://localhost:11434";
        std::string default_model = "llama3";
        std::chrono::milliseconds timeout{120000};
        net::TlsConfig tls;
        net::RetryPolicy retry;
    };

    explicit OllamaProvider(Config config);

    std::string name() const override { return "ollama"; }
    std::vector<std::string> list_models() const override;
    ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
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
    mutable net::HttpClient client_;
};

}  // namespace ea::provider
