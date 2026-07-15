// BuildToolSpecsStep — assemble tool specs from active Toolsets
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class BuildToolSpecsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        if (ctx.registry) {
            ctx.tool_specs = ctx.registry->active_specs();
        }
        return {};
    }

    std::string name() const override { return "build_tool_specs"; }
};

}  // namespace ea::agent
