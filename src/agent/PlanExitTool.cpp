#include "PlanExitTool.h"
#include "base/Error.h"

namespace ea::agent {

Result<ToolResult> PlanExitTool::execute(const json&) {
    if (!state_ || state_->mode.load() != PermissionMode::Plan) {
        return ToolResult{"", "Not in plan mode — plan_exit is only available while planning.", true};
    }

    security::ApprovalRequest req;
    req.tool_name = "plan_exit";
    req.description = "Planning is complete. Switch to build mode and start implementing the plan?";

    auto decision = approval_
        ? approval_->request_approval(req)
        : security::ApprovalDecision::Approved;

    if (decision == security::ApprovalDecision::Approved) {
        // Restore the non-plan mode saved when planning started.
        PermissionMode restored = state_->previous.load();
        if (restored == PermissionMode::Plan) restored = PermissionMode::Default;
        state_->mode.store(restored);
        state_->exit_approved.store(true);
        return ToolResult{"", "Plan approved. Switching to build mode to implement the plan.", false};
    }
    if (decision == security::ApprovalDecision::Aborted) {
        return ToolResult{"", "Plan approval aborted.", true};
    }
    return ToolResult{"", "User chose to continue planning. Keep refining the plan.", false};
}

}  // namespace ea::agent