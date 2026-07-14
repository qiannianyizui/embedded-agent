#include "OllamaProvider.h"
#include "common/base/Error.h"

namespace ea::provider {

OllamaProvider::OllamaProvider(Config config)
    : config_(std::move(config)) {}

std::vector<std::string> OllamaProvider::list_models() const {
    return {};
}

Result<LLMResponse> OllamaProvider::chat(
    const std::vector<Message>& /*messages*/,
    const std::vector<ToolSpec>& /*tools*/,
    const std::string& /*model*/,
    const ChatOptions& /*opts*/
) {
    return Error::not_found("Ollama provider not yet implemented");
}

Result<void> OllamaProvider::stream_chat(
    const std::vector<Message>& /*messages*/,
    const std::vector<ToolSpec>& /*tools*/,
    const std::string& /*model*/,
    std::function<void(const StreamChunk&)> /*on_chunk*/,
    const ChatOptions& /*opts*/
) {
    return Error::not_found("Ollama provider not yet implemented");
}

}  // namespace ea::provider
