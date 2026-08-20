// PlanExitTool — model-callable handoff from plan mode back to build mode
#pragma once
#include "tool/ITool.h"
#include "security/IApprovalHandler.h"
#include "PermissionMode.h"
#include <memory>

namespace ea::agent {

class PlanExitTool : public ITool {
public:
    PlanExitTool(security::IApprovalHandler* approval,
                 std::shared_ptr<PermissionState> state)
        : approval_(approval), state_(std::move(state)) {}

    std::string name() const override { return "plan_exit"; }
    std::string description() const override {
        return "Signal that planning is complete and request approval to switch "
               "to build mode and start implementing. Only call this after the "
               "final plan has been written to the plan file.";
    }
    json parameters_schema() const override {
        return json::parse(R"({"type":"object","properties":{},"additionalProperties":false})");
    }
    Result<ToolResult> execute(const json& args) override;

    bool is_mutating() const override { return false; }
    bool is_dangerous() const override { return false; }

private:
    security::IApprovalHandler* approval_;
    std::shared_ptr<PermissionState> state_;
};

}  // namespace ea::agent
