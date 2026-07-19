// Memory strategy interface — pluggable multi-turn memory management
#pragma once
#include "core/Types.h"
#include "core/IMemory.h"
#include "core/IProvider.h"
#include <string>
#include <vector>

namespace ea::agent {

struct MemoryStrategyContext {
    std::vector<Message>& history;        // Current conversation (mutable — strategy may prune)
    IMemory* memory;                      // Memory backend
    IProvider* provider;                  // LLM provider (for extraction/summarization)
    const std::string& user_input;        // This turn's user input
    const std::string& assistant_output;  // This turn's assistant output
};

class IMemoryStrategy {
public:
    virtual ~IMemoryStrategy() = default;

    // Before each turn: inject memories into system prompt
    virtual std::string build_memory_prompt(const std::vector<Message>& history,
                                             IMemory* memory) = 0;

    // After each turn: extract facts + summarize old messages
    virtual void on_turn_end(MemoryStrategyContext& ctx) = 0;

    // Query whether strategy is active
    virtual bool is_active() const = 0;
};

// Null object — no-op strategy for explicit opt-out
class NullMemoryStrategy : public IMemoryStrategy {
public:
    std::string build_memory_prompt(const std::vector<Message>&, IMemory*) override {
        return {};
    }
    void on_turn_end(MemoryStrategyContext&) override {}
    bool is_active() const override { return false; }
};

}  // namespace ea::agent
