// ExecuteToolsStep — execute pending tool calls with security + approval checks
#pragma once
#include "agent/ITurnStep.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"

namespace ea::agent {

class ExecuteToolsStep : public ITurnStep {
public:
    ExecuteToolsStep(security::SecurityPolicy* policy = nullptr,
                     security::IApprovalHandler* approval = nullptr);

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "execute_tools"; }

private:
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;
};

}  // namespace ea::agent
