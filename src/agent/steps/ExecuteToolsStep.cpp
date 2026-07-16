#include "ExecuteToolsStep.h"
#include "tool/ToolOutputConfig.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> ExecuteToolsStep::execute(TurnContext& ctx) {
    if (ctx.pending_tool_calls.empty()) {
        return {};
    }

    if (!ctx.registry) {
        return Error::invalid_arg("No tool registry configured");
    }

    ctx.tool_results.clear();

    tool::ToolOutputConfig config;
    config.max_bytes = static_cast<size_t>(ctx.max_tool_output_bytes);

    for (const auto& tc : ctx.pending_tool_calls) {
        EA_DEBUG("Executing tool: {} (id: {})", tc.name, tc.id);

        auto result = ctx.registry->execute(tc.name, tc.arguments);
        ToolResult tool_result;
        if (result.ok()) {
            tool_result = std::move(result.value());
        } else {
            tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
        }

        // Truncate output using ToolOutputConfig
        tool_result.output = tool::truncate_output(
            tool_result.output, config.get_limit(tc.name), config.truncate_marker);

        ctx.tool_results.push_back(std::move(tool_result));

        EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                 ctx.tool_results.back().output.size(), ctx.tool_results.back().is_error);
    }

    return {};
}

}  // namespace ea::agent
