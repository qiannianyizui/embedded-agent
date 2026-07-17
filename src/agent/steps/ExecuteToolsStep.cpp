#include "ExecuteToolsStep.h"
#include "agent/AgentEvent.h"
#include "tool/ToolOutputConfig.h"
#include "common/io/Logger.h"

namespace ea::agent {

ExecuteToolsStep::ExecuteToolsStep(security::SecurityPolicy* policy,
                                   security::IApprovalHandler* approval)
    : policy_(policy), approval_(approval) {}

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

        // 1. SecurityPolicy hard check
        if (policy_) {
            auto check = policy_->check_tool(tc.name);
            if (!check.ok()) {
                EA_WARN("Tool blocked by security policy: {}", tc.name);
                ctx.tool_results.push_back(
                    ToolResult{tc.id, "Blocked by security policy: " + check.error().message, true});
                continue;
            }
        }

        // 2. Approval soft check for dangerous tools
        ITool* tool = ctx.registry->find(tc.name);
        if (tool && tool->is_dangerous() && approval_) {
            security::ApprovalRequest req;
            req.tool_name = tc.name;
            req.arguments = tc.arguments;
            req.description = "Tool '" + tc.name + "' is marked as dangerous";

            auto decision = approval_->request_approval(req);
            if (decision == security::ApprovalDecision::Rejected) {
                EA_WARN("Tool rejected by user: {}", tc.name);
                ctx.tool_results.push_back(
                    ToolResult{tc.id, "User rejected this tool call", true});
                continue;
            }
            if (decision == security::ApprovalDecision::Aborted) {
                EA_WARN("User aborted agent loop during approval");
                ctx.should_stop = true;
                return {};
            }
            // Approved — continue to execute
        }

        // Emit ToolCallStart event
        if (ctx.emit_fn) {
            AgentEvent event;
            event.type = AgentEventType::ToolCallStart;
            event.iteration = ctx.iteration;
            event.agent_id = ctx.agent_id;
            event.tool_name = tc.name;
            event.tool_arguments = tc.arguments;
            ctx.emit_fn(event);
        }

        // 3. Execute the tool
        auto result = ctx.registry->execute(tc.name, tc.arguments);
        ToolResult tool_result;
        if (result.ok()) {
            tool_result = std::move(result.value());
        } else {
            tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
        }

        // 4. Truncate output using ToolOutputConfig
        tool_result.output = tool::truncate_output(
            tool_result.output, config.get_limit(tc.name), config.truncate_marker);

        ctx.tool_results.push_back(std::move(tool_result));

        EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                 ctx.tool_results.back().output.size(), ctx.tool_results.back().is_error);

        // Emit ToolCallEnd event
        if (ctx.emit_fn) {
            AgentEvent event;
            event.type = AgentEventType::ToolCallEnd;
            event.iteration = ctx.iteration;
            event.agent_id = ctx.agent_id;
            event.tool_name = tc.name;
            event.tool_result = ctx.tool_results.back().output;
            event.tool_error = ctx.tool_results.back().is_error;
            ctx.emit_fn(event);
        }
    }

    return {};
}

}  // namespace ea::agent
