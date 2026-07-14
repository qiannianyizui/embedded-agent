#pragma once
#include "core/IProvider.h"
#include "common/net/HttpClient.h"

namespace ea::provider {

class OpenAIProvider : public IProvider {
public:
    struct Config {
        std::string base_url = "https://api.openai.com/v1";
        std::string api_key;
        std::string default_model = "gpt-4o";
        net::TlsConfig tls;
        net::RetryPolicy retry;
        std::chrono::milliseconds timeout{60000};
    };

    explicit OpenAIProvider(Config config);

    std::string name() const override { return "openai_compatible"; }
    std::vector<std::string> list_models() const override;

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
    void parse_sse_chunk(const json& delta,
                         std::vector<ToolCall>& accumulating_calls,
                         std::string& content,
                         std::function<void(const StreamChunk&)> on_chunk) const;

private:
    Config config_;
    net::HttpClient client_;
};

}  // namespace ea::provider
