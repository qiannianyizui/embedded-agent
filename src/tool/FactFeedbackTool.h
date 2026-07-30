#pragma once
// FactFeedbackTool — LLM tool for recording trust feedback on facts
// 2 actions: helpful, unhelpful

#include "tool/ITool.h"
#include "memory/IMemory.h"

namespace ea::tool {

class FactFeedbackTool : public ITool {
public:
    explicit FactFeedbackTool(IMemory* memory) : memory_(memory) {}

    std::string name() const override { return "fact_feedback"; }
    std::string description() const override {
        return "Record feedback on stored facts to adjust trust scores. "
               "Positive feedback increases trust, negative decreases it (asymmetric).";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }

private:
    IMemory* memory_;
};

}  // namespace ea::tool
