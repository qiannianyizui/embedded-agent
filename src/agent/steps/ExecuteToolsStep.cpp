#include "ExecuteToolsStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> ExecuteToolsStep::execute(TurnContext& ctx) {
    if (ctx.pending_tool_calls.empty()) {
        return {};
    }

    if (!ctx.registry) {
        return Error{ErrorCode::InvalidArgument, "No tool registry configured", 0, {}};
    }

    ctx.tool_results.clear();

    for (const auto& tc : ctx.pending_tool_calls) {
        EA_DEBUG("Executing tool: {} (id: {})", tc.name, tc.id);

        auto result = ctx.registry->execute(tc.name, tc.arguments);
        ToolResult tool_result;
        if (result.ok()) {
            tool_result = std::move(result.value());
        } else {
            tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
        }

        // Truncate output if too long
        if (static_cast<int>(tool_result.output.size()) > ctx.max_tool_output_bytes) {
            tool_result.output = tool_result.output.substr(0, ctx.max_tool_output_bytes)
                                 + "\n... [truncated]";
        }

        ctx.tool_results.push_back(std::move(tool_result));

        EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                 ctx.tool_results.back().output.size(), ctx.tool_results.back().is_error);
    }

    return {};
}

}  // namespace ea::agent
