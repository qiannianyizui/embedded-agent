#pragma once
#include "core/ITool.h"
#include "core/IMemory.h"

namespace ea::tool {

class MemoryTool : public ITool {
public:
    explicit MemoryTool(IMemory* memory) : memory_(memory) {}

    std::string name() const override { return "memory"; }
    std::string description() const override { return "Memory store, recall, and forget operations"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;

private:
    IMemory* memory_;
};

}  // namespace ea::tool
