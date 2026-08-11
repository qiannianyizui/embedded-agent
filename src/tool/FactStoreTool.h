#pragma once
// FactStoreTool — LLM tool for structured fact storage and algebraic queries
// 9 actions: add, search, probe, related, reason, contradict, update, remove, list

#include "tool/ITool.h"
#include "memory/IMemory.h"
#include "memory/CuratedMemoryStore.h"

namespace ea::tool {

class FactStoreTool : public ITool {
public:
    explicit FactStoreTool(IMemory* memory,
                           memory::CuratedMemoryStore* curated = nullptr)
        : memory_(memory), curated_(curated) {}

    std::string name() const override { return "fact_store"; }
    std::string description() const override {
        return "Deep structured memory with algebraic reasoning. "
               "Use alongside the memory tool — memory for always-on context "
               "(USER.md/MEMORY.md), fact_store for deep recall and "
               "compositional queries.\n\n"
               "ACTIONS (simple → powerful):\n"
               "• add — Store a fact the user would expect you to remember.\n"
               "• search — Keyword lookup ('editor config', 'deploy process').\n"
               "• probe — Entity recall: ALL facts about a person/thing.\n"
               "• related — What connects to an entity? Structural adjacency.\n"
               "• reason — Compositional: facts connected to MULTIPLE entities "
               "simultaneously.\n"
               "• contradict — Memory hygiene: find facts making conflicting claims.\n"
               "• update/remove/list — CRUD operations.\n\n"
               "IMPORTANT: Before answering questions about the user, ALWAYS "
               "probe or reason first.";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }

private:
    IMemory* memory_;
    memory::CuratedMemoryStore* curated_;
};

}  // namespace ea::tool
