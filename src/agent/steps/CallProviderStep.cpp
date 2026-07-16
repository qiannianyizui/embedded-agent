#include "CallProviderStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

CallProviderStep::CallProviderStep(ContextCompressor* compressor)
    : compressor_(compressor) {}

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::invalid_arg("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
    }

    // Compress if needed
    if (compressor_) {
        auto compressed = compressor_->compress(messages);
        if (compressed.ok()) {
            messages = std::move(compressed.value());
        }
        // If compression fails, use original messages (graceful degradation)
    }

    ChatOptions opts;
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) {
        EA_ERROR("LLM call failed: {}", response.error().message);
        return response.error();
    }

    ctx.response = std::move(response.value());
    return {};
}

}  // namespace ea::agent
