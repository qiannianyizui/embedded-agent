#include "CallProviderStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error{ErrorCode::InvalidArgument, "No provider configured", 0, {}};
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
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
