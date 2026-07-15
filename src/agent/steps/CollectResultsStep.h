// CollectResultsStep — append tool results to message history
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class CollectResultsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        for (size_t i = 0; i < ctx.tool_results.size(); ++i) {
            const auto& result = ctx.tool_results[i];
            std::string tool_name;
            std::string call_id;

            // Match result to its tool call
            if (i < ctx.pending_tool_calls.size()) {
                tool_name = ctx.pending_tool_calls[i].name;
                call_id = ctx.pending_tool_calls[i].id;
            }

            Message tool_msg{Role::Tool, result.output, tool_name, std::nullopt, call_id};
            ctx.messages.push_back(std::move(tool_msg));
        }

        // Clear per-iteration state
        ctx.pending_tool_calls.clear();
        ctx.tool_results.clear();

        return {};
    }

    std::string name() const override { return "collect_results"; }
};

}  // namespace ea::agent
