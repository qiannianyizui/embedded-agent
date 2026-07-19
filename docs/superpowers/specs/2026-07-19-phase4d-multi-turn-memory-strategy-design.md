# Phase 4D: Multi-turn Memory Strategy — Design Spec

**Date**: 2026-07-19
**Status**: Draft
**Depends on**: Phase 4A (OllamaProvider), Phase 4B (Server Mode)

## Problem

As conversations grow longer, token usage increases unboundedly. The existing mechanisms are passive and lossy:

- **ContextCompressor**: triggers only when tokens exceed a threshold; summarization is a one-way lossy operation
- **HistoryPruneStep**: hard-truncates messages with a breadcrumb, losing all information
- **IMemory.on_turn_end()**: no-op placeholder — no automatic fact extraction or summarization

The result: long conversations either hit token limits (causing degraded compression) or lose important context irrecoverably.

## Solution

Introduce an **IMemoryStrategy** interface with a **ProgressiveMemoryStrategy** implementation that performs active, incremental memory management every turn:

1. **Extract key facts** from each turn (LLM-powered) → store in long-term memory
2. **Summarize old messages** proactively → store in short-term memory
3. **Inject memories** into system prompt → keep context rich without token bloat

This is the "active defense" layer; ContextCompressor remains as the "safety net" when tokens still exceed thresholds.

## Architecture

### IMemoryStrategy Interface

```cpp
// src/agent/IMemoryStrategy.h
namespace ea::agent {

struct MemoryStrategyContext {
    std::vector<Message>& history;        // Current conversation (mutable)
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

}  // namespace ea::agent
```

### Three-Layer Memory Model

Reuses existing `IMemory` with `category` field to distinguish layers:

| Layer | category | Retention | Retrieval |
|-------|----------|-----------|-----------|
| Working | — (history_) | Last N turns, full fidelity | Direct in conversation history |
| Short-term | `short_term` | Recent summaries, capped at M entries | `recall(query, limit)` |
| Long-term | `long_term` | Permanent, importance ≥ 7 | `recall(query, limit)` + FTS5 |

**Promotion rules**:
- Working → Short-term: when history exceeds `working_turns * 2` messages, oldest messages are summarized and stored as `short_term`
- Short-term → Long-term: LLM-extracted key facts are stored directly as `long_term` with importance=8
- Short-term eviction: when short_term entries exceed `short_term_max`, oldest are forgotten

### ProgressiveMemoryStrategy

```cpp
struct ProgressiveMemoryConfig {
    int working_turns = 6;            // Working memory: keep last N turns (1 turn = user + assistant)
    int short_term_max = 20;          // Short-term memory: max entries before eviction
    int long_term_importance = 8;     // Importance level for long-term facts
    bool enable_fact_extraction = true;   // Enable LLM key fact extraction
    bool enable_auto_summarize = true;    // Enable automatic summarization of old messages
    std::string fact_extraction_prompt;   // Customizable extraction prompt (default: see Default Prompts below)
    std::string summarization_prompt;     // Customizable summarization prompt (default: see Default Prompts below)
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
    // Step 1: Extract key facts from this turn
    void extract_facts(MemoryStrategyContext& ctx);

    // Step 2: Summarize old working memory into short-term
    void summarize_old_messages(MemoryStrategyContext& ctx);

    // Step 3: Evict excess short-term entries
    void evict_short_term(IMemory* memory);

    ProgressiveMemoryConfig config_;
};
```

### on_turn_end Flow

```
1. Fact Extraction (LLM, conditional on enable_fact_extraction)
   Input:  user_input + assistant_output
   Output: Structured fact list
   Action: Each fact → memory->store(content, "long_term", importance=8)
   Failure: Skip, log warning, do not block

2. Working → Short-term (conditional on enable_auto_summarize)
   Condition: history.size() > working_turns * 2
   Action:
     a. Take oldest (history.size() - working_turns*2) messages
     b. LLM summarize them
     c. memory->store(summary, "short_term", importance=5)
     d. Remove summarized messages from history (in-place via ctx.history reference)
     e. Insert [Summary] breadcrumb message at removal point
   Failure: Fall back to simple truncation (keep head + tail)

3. Short-term Eviction
   Condition: short_term count > short_term_max
   Action: Forget oldest (count - short_term_max) entries
   Failure: Log warning, skip
```

### build_memory_prompt Flow

```
1. Query long-term: extract last user message from history, then memory->recall(last_user_msg, 3) → key facts
2. Query short-term: memory->list(short_term_max, 0) → recent summaries
3. Assemble:
   "# Conversation Context\n
    ## Key Facts\n{long-term entries}\n
    ## Recent Summary\n{short-term entries}\n"
4. Return empty string if no memories found
```

### AgentLoop Integration

AgentLoop gains an `IMemoryStrategy* strategy_` member (default nullptr):

- **build_system_prompt_once()**: after building the base system prompt, if `strategy_` is set, call `strategy_->build_memory_prompt(history_, memory_)` and append the result
- **run()**: after the loop exits with `should_stop`, if `strategy_` is set, construct `MemoryStrategyContext` and call `strategy_->on_turn_end(ctx)`
- **strategy_ == nullptr**: existing behavior unchanged (zero impact)

### Relationship with ContextCompressor

- **ProgressiveMemoryStrategy** = active defense: incremental compression every turn, keeps tokens below threshold
- **ContextCompressor** = safety net: if progressive strategy doesn't reduce tokens enough, CallProviderStep's compressor still fires
- Both coexist; progressive strategy reduces the frequency and severity of compressor triggers

### NullMemoryStrategy

```cpp
class NullMemoryStrategy : public IMemoryStrategy {
public:
    std::string build_memory_prompt(const std::vector<Message>&, IMemory*) override { return {}; }
    void on_turn_end(MemoryStrategyContext&) override {}
    bool is_active() const override { return false; }
};
```

For explicit opt-out or testing without memory strategy.

## Error Handling & Degradation

| Scenario | Handling |
|----------|----------|
| LLM fact extraction fails | Skip extraction, log warning, conversation continues |
| LLM summarization fails | Fall back to simple truncation (keep head + tail, drop middle) |
| memory->store() fails | Log warning, skip storage, do not block |
| memory->recall() fails | Return empty memory block, system prompt has no memory injection |
| strategy_ is nullptr | Original AgentLoop behavior, zero impact |

**Core principle**: Memory strategy never blocks the main conversation flow. All LLM calls and storage operations degrade gracefully.

## File Structure

```
src/agent/
  IMemoryStrategy.h              — Interface + MemoryStrategyContext
  ProgressiveMemoryStrategy.h    — Class declaration + config
  ProgressiveMemoryStrategy.cpp  — Implementation
  AgentLoop.h / .cpp             — Add strategy_ member and call sites

tests/
  test_memory_strategy.cpp       — Unit + integration tests
```

## Test Plan

| Type | Coverage | Tags |
|------|----------|------|
| Unit | IMemoryStrategy interface, ProgressiveMemoryConfig | `[memory]` `[strategy]` |
| Unit | ProgressiveMemoryStrategy.on_turn_end (MockProvider + InMemoryBackend) | `[memory]` `[strategy]` |
| Unit | ProgressiveMemoryStrategy.build_memory_prompt | `[memory]` `[strategy]` |
| Unit | Degradation scenarios (LLM failure, storage failure) | `[memory]` `[strategy]` |
| Unit | Promotion rules (working → short-term → long-term) | `[memory]` `[strategy]` |
| Unit | NullMemoryStrategy | `[memory]` `[strategy]` |
| Integration | AgentLoop + ProgressiveMemoryStrategy end-to-end | `[agent]` `[memory]` |
| Integration | Coexistence with ContextCompressor | `[agent]` `[memory]` |

## Configuration

Add to `AppConfig` under `[memory]` TOML section:

```toml
[memory.strategy]
type = "progressive"          # "progressive" | "none"
working_turns = 6
short_term_max = 20
long_term_importance = 8
enable_fact_extraction = true
enable_auto_summarize = true
```

## Namespace

`ea::agent` — memory strategy is an agent-level concern, not a memory-backend concern.

## Default Prompts

**Fact extraction prompt** (used when `fact_extraction_prompt` is empty):
```
Extract key facts from this conversation turn. Return each fact as a separate line starting with "- ".
Focus on: user preferences, decisions made, important entities, constraints, and outcomes.
Omit: greetings, acknowledgments, and routine exchanges.

User: {user_input}
Assistant: {assistant_output}
```

**Summarization prompt** (used when `summarization_prompt` is empty):
```
Summarize the following conversation segment concisely, preserving key facts, decisions, and outcomes.
Omit greetings and repetitions. Focus on information that would be needed in future turns.
```

## Out of Scope

- Memory visualization / debugging UI
- Cross-session memory sharing (already handled by ScopedMemory)
- Custom extraction/summarization models (uses the same provider as the agent)
- Memory versioning / rollback
