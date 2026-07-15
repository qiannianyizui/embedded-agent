#include "LoopDetectStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> LoopDetectStep::execute(TurnContext& ctx) {
    // Only check if we have pending tool calls and results
    if (ctx.pending_tool_calls.empty() || ctx.tool_results.empty()) {
        return {};
    }

    // Check the last tool call + result
    const auto& last_call = ctx.pending_tool_calls.back();
    const auto& last_result = ctx.tool_results.back();

    auto action = detector_.check(last_call, last_result);

    switch (action) {
    case LoopAction::Continue:
        break;
    case LoopAction::Warn:
        EA_WARN("Loop detected: ping-pong pattern");
        if (warn_) warn_("Loop detected: alternating tool calls detected");
        break;
    case LoopAction::Block:
        EA_WARN("Loop detected: exact repeat, blocking tool call");
        ctx.pending_tool_calls.clear();
        ctx.tool_results.clear();
        // Add a warning message to history
        ctx.messages.push_back({Role::System,
            "[System: Repeated identical tool call detected. Breaking loop.]", std::nullopt, std::nullopt, std::nullopt});
        break;
    case LoopAction::Break:
        EA_WARN("Loop detected: no progress, breaking loop");
        ctx.should_stop = true;
        break;
    }

    return {};
}

}  // namespace ea::agent
