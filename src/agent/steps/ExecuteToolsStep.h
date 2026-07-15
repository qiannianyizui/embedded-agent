// ExecuteToolsStep — execute pending tool calls
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class ExecuteToolsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "execute_tools"; }
};

}  // namespace ea::agent
