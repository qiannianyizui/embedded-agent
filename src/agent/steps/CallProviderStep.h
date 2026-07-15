// CallProviderStep — invoke LLM provider
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class CallProviderStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "call_provider"; }
};

}  // namespace ea::agent
