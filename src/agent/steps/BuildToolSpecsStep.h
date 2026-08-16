// BuildToolSpecsStep — assemble tool specs from active Toolsets
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class BuildToolSpecsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        if (ctx.registry) {
            auto specs = ctx.registry->active_specs();
            if (!ctx.plan_mode) {
                for (const auto& spec : specs) {
                    if (spec.name != "plan_exit") {
                        ctx.tool_specs.push_back(spec);
                    }
                }
                return {};
            }
            for (const auto& spec : specs) {
                ITool* tool = ctx.registry->find(spec.name);
                if (!tool) continue;
                // Plan mode: read-only tools, the file tool (plan file only,
                // enforced in ExecuteToolsStep) and plan_exit stay visible.
                if (!tool->is_mutating() || spec.name == "file" || spec.name == "plan_exit") {
                    ctx.tool_specs.push_back(spec);
                }
            }
        }
        return {};
    }

    std::string name() const override { return "build_tool_specs"; }
};

}  // namespace ea::agent
