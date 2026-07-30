#pragma once
// FactStoreTool — LLM tool for structured fact storage and algebraic queries
// 9 actions: add, search, probe, related, reason, contradict, update, remove, list

#include "tool/ITool.h"
#include "memory/IMemory.h"

namespace ea::tool {

class FactStoreTool : public ITool {
public:
    explicit FactStoreTool(IMemory* memory) : memory_(memory) {}

    std::string name() const override { return "fact_store"; }
    std::string description() const override {
        return "Structured fact storage with entity knowledge graph, trust scoring, "
               "and algebraic queries (probe/related/reason/contradict)";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }

private:
    IMemory* memory_;
};

}  // namespace ea::tool
