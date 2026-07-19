// ProgressiveMemoryStrategy — active, incremental memory management
// Extract facts → summarize → inject across turns
#pragma once
#include "IMemoryStrategy.h"
#include <string>

namespace ea::agent {

struct ProgressiveMemoryConfig {
    int working_turns = 6;                // Keep last N turns in working memory (1 turn = user + assistant)
    int short_term_max = 20;              // Max short-term memory entries before eviction
    int long_term_importance = 8;         // Importance level for extracted facts
    bool enable_fact_extraction = true;   // Enable LLM key fact extraction
    bool enable_auto_summarize = true;    // Enable automatic summarization of old messages
    std::string fact_extraction_prompt;   // Custom extraction prompt (empty = use default)
    std::string summarization_prompt;     // Custom summarization prompt (empty = use default)
};

class ProgressiveMemoryStrategy : public IMemoryStrategy {
public:
    explicit ProgressiveMemoryStrategy(ProgressiveMemoryConfig config = {});

    std::string build_memory_prompt(const std::vector<Message>& history,
                                     IMemory* memory) override;
    void on_turn_end(MemoryStrategyContext& ctx) override;
    bool is_active() const override { return true; }

    const ProgressiveMemoryConfig& config() const { return config_; }

private:
    // Extract key facts from this turn using LLM
    void extract_facts(MemoryStrategyContext& ctx);

    // Summarize old working memory into short-term
    void summarize_old_messages(MemoryStrategyContext& ctx);

    // Evict excess short-term entries
    void evict_short_term(IMemory* memory);

    // Get the effective extraction prompt
    std::string get_extraction_prompt() const;

    // Get the effective summarization prompt
    std::string get_summarization_prompt() const;

    ProgressiveMemoryConfig config_;
};

}  // namespace ea::agent
