// LoopDetectStep — check for loop patterns after tool execution
#pragma once
#include "agent/ITurnStep.h"
#include "agent/LoopDetector.h"
#include <functional>

namespace ea::agent {

class LoopDetectStep : public ITurnStep {
public:
    using WarnFn = std::function<void(const std::string&)>;

    explicit LoopDetectStep(LoopDetector& detector, WarnFn warn = nullptr)
        : detector_(detector), warn_(std::move(warn)) {}

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "loop_detect"; }

private:
    LoopDetector& detector_;
    WarnFn warn_;
};

}  // namespace ea::agent
