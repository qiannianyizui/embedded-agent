#pragma once
#include "tool/ITool.h"
#include "memory/IMemory.h"
#include "memory/CuratedMemoryStore.h"

namespace ea::tool {

class MemoryTool : public ITool {
public:
    explicit MemoryTool(IMemory* memory,
                        memory::CuratedMemoryStore* curated = nullptr)
        : memory_(memory), curated_(curated) {}

    std::string name() const override { return "memory"; }
    std::string description() const override { return "Memory store, recall, and forget operations"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }  // store/forget are write ops

private:
    IMemory* memory_;
    memory::CuratedMemoryStore* curated_;
};

}  // namespace ea::tool
