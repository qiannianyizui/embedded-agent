#pragma once
#include "core/IProvider.h"

namespace ea::provider {

class OllamaProvider : public IProvider {
public:
    struct Config {
        std::string base_url = "http://localhost:11434";
        std::chrono::milliseconds timeout{120000};
    };

    explicit OllamaProvider(Config config);

    std::string name() const override { return "ollama"; }
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

private:
    Config config_;
};

}  // namespace ea::provider
