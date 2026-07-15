// ParseResponseStep — parse LLM response, extract tool calls
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class ParseResponseStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        // Add assistant message to history
        Message assistant_msg{Role::Assistant, ctx.response.content, std::nullopt, std::nullopt, std::nullopt};
        if (!ctx.response.tool_calls.empty()) {
            assistant_msg.tool_calls = ctx.response.tool_calls;
        }
        ctx.messages.push_back(std::move(assistant_msg));

        // If no tool calls, we're done
        if (!ctx.response.is_tool_use() || ctx.response.tool_calls.empty()) {
            ctx.should_stop = true;
        } else {
            ctx.pending_tool_calls = ctx.response.tool_calls;
        }

        return {};
    }

    std::string name() const override { return "parse_response"; }
};

}  // namespace ea::agent
