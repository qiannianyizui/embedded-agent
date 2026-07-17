// DelegateTool — ITool bridge for LLM-driven sub-agent delegation
#pragma once
#include "core/ITool.h"
#include "SubagentOrchestrator.h"

namespace ea::agent {

class DelegateTool : public ITool {
public:
    explicit DelegateTool(SubagentOrchestrator* orchestrator)
        : orchestrator_(orchestrator) {}

    std::string name() const override { return "delegate"; }

    std::string description() const override {
        return "Delegate a subtask to a specialized sub-agent. "
               "Use for tasks that benefit from a different perspective, "
               "expertise, or independent execution.";
    }

    json parameters_schema() const override {
        return json::parse(R"({
            "type": "object",
            "properties": {
                "agent": {
                    "type": "string",
                    "description": "Name of the sub-agent template to use"
                },
                "task": {
                    "type": "string",
                    "description": "Description of the task to delegate"
                },
                "parallel": {
                    "type": "boolean",
                    "description": "Execute multiple sub-tasks in parallel",
                    "default": false
                },
                "tasks": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "agent": {"type": "string"},
                            "task": {"type": "string"}
                        },
                        "required": ["agent", "task"]
                    },
                    "description": "Multiple sub-tasks for parallel execution"
                }
            },
            "required": ["agent", "task"]
        })");
    }

    Result<ToolResult> execute(const json& args) override {
        bool parallel = args.value("parallel", false);

        if (parallel && args.contains("tasks") && args["tasks"].is_array()) {
            std::vector<std::pair<std::string, std::string>> tasks;
            for (const auto& t : args["tasks"]) {
                tasks.push_back({t.value("agent", ""), t.value("task", "")});
            }

            auto result = orchestrator_->delegate_parallel(tasks);
            if (!result.ok()) {
                return ToolResult{"", "Parallel delegation failed: " + result.error().message, true};
            }

            std::string output;
            for (const auto& [name, res] : result.value()) {
                output += "## " + name + "\n" + res + "\n\n";
            }
            return ToolResult{"", output, false};
        }

        // Single delegation
        std::string agent_name = args.value("agent", "");
        std::string task = args.value("task", "");

        auto result = orchestrator_->delegate(agent_name, task);
        if (!result.ok()) {
            return ToolResult{"", "Delegation failed: " + result.error().message, true};
        }
        return ToolResult{"", result.value(), false};
    }

    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    SubagentOrchestrator* orchestrator_;
};

}  // namespace ea::agent
