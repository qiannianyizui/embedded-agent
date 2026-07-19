# Phase 4D: Multi-turn Memory Strategy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an IMemoryStrategy interface and ProgressiveMemoryStrategy implementation that performs active, incremental memory management every turn — extracting key facts, summarizing old messages, and injecting memories into the system prompt.

**Architecture:** Strategy pattern — IMemoryStrategy interface with ProgressiveMemoryStrategy (three-layer: working/short-term/long-term) and NullMemoryStrategy. AgentLoop delegates to strategy at two points: before each turn (build_memory_prompt) and after each turn (on_turn_end). Reuses existing IMemory backend with category field to distinguish layers.

**Tech Stack:** C++17, nlohmann/json, spdlog, Catch2, existing IProvider/IMemory interfaces

## Global Constraints

- Namespace: `ea::agent`
- Error construction: Always initialize all Error fields via factory methods (`Error::net()`, `Error::invalid_arg()`, etc.)
- Object libraries: Add to `ea-agent` OBJECT library in `src/agent/CMakeLists.txt`
- Test tags: `[memory]` `[strategy]` for unit tests; `[agent]` `[memory]` for integration tests
- Memory strategy never blocks the main conversation flow — all LLM/storage failures degrade gracefully
- Category values for memory layers: `"short_term"` and `"long_term"` (verbatim)
- ProgressiveMemoryConfig defaults: `working_turns=6`, `short_term_max=20`, `long_term_importance=8`
- NullMemoryStrategy::is_active() returns `false`; ProgressiveMemoryStrategy::is_active() returns `true`
- build_memory_prompt returns `"# Conversation Context\n## Key Facts\n...\n## Recent Summary\n...\n"` format (verbatim header structure)
- Fact extraction stores each fact individually via `memory->store(content, "long_term", importance=8)`
- Summarization stores one summary via `memory->store(summary, "short_term", importance=5)`
- Short-term eviction uses `memory->list()` + `memory->forget()` by ID

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/agent/IMemoryStrategy.h` | Interface + MemoryStrategyContext + NullMemoryStrategy |
| `src/agent/ProgressiveMemoryStrategy.h` | ProgressiveMemoryConfig + ProgressiveMemoryStrategy declaration |
| `src/agent/ProgressiveMemoryStrategy.cpp` | Implementation of extract_facts, summarize_old_messages, evict_short_term, build_memory_prompt, on_turn_end |
| `src/agent/AgentLoop.h` | Add `IMemoryStrategy* strategy_` member + constructor param |
| `src/agent/AgentLoop.cpp` | Add strategy call sites in build_system_prompt_once() and run() |
| `src/agent/CMakeLists.txt` | Add ProgressiveMemoryStrategy.cpp to ea-agent |
| `src/config/Config.h` | Add MemoryStrategyConfig struct, add field to AppConfig |
| `src/config/Config.cpp` | Parse `[memory.strategy]` TOML section |
| `src/main.cpp` | Create and wire ProgressiveMemoryStrategy |
| `tests/test_memory_strategy.cpp` | Unit + integration tests |
| `tests/CMakeLists.txt` | Add test file |

---

### Task 1: IMemoryStrategy Interface + NullMemoryStrategy

**Files:**
- Create: `src/agent/IMemoryStrategy.h`

**Interfaces:**
- Consumes: `ea::Message` from `core/Types.h`, `ea::IMemory` from `core/IMemory.h`, `ea::IProvider` from `core/IProvider.h`
- Produces: `IMemoryStrategy` interface, `MemoryStrategyContext` struct, `NullMemoryStrategy` class

- [ ] **Step 1: Create IMemoryStrategy.h**

```cpp
// src/agent/IMemoryStrategy.h
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
```

- [ ] **Step 2: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds (header-only, no consumers yet)

- [ ] **Step 3: Commit**

```bash
git add src/agent/IMemoryStrategy.h
git commit -m "feat: add IMemoryStrategy interface and NullMemoryStrategy"
```

---

### Task 2: ProgressiveMemoryStrategy Header + Config

**Files:**
- Create: `src/agent/ProgressiveMemoryStrategy.h`

**Interfaces:**
- Consumes: `IMemoryStrategy` from Task 1
- Produces: `ProgressiveMemoryConfig` struct, `ProgressiveMemoryStrategy` class declaration

- [ ] **Step 1: Create ProgressiveMemoryStrategy.h**

```cpp
// src/agent/ProgressiveMemoryStrategy.h
// Active, incremental memory management: extract facts → summarize → inject
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
```

- [ ] **Step 2: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds (header-only, no .cpp yet)

- [ ] **Step 3: Commit**

```bash
git add src/agent/ProgressiveMemoryStrategy.h
git commit -m "feat: add ProgressiveMemoryStrategy header and config"
```

---

### Task 3: ProgressiveMemoryStrategy Implementation

**Files:**
- Create: `src/agent/ProgressiveMemoryStrategy.cpp`
- Modify: `src/agent/CMakeLists.txt` — add ProgressiveMemoryStrategy.cpp

**Interfaces:**
- Consumes: `ProgressiveMemoryConfig`, `IMemoryStrategy`, `MemoryStrategyContext` from Tasks 1-2; `IProvider::chat()`, `IMemory::store/recall/list/forget()`, `Logger.h`
- Produces: Full ProgressiveMemoryStrategy implementation

- [ ] **Step 1: Write ProgressiveMemoryStrategy.cpp**

```cpp
// src/agent/ProgressiveMemoryStrategy.cpp
#include "ProgressiveMemoryStrategy.h"
#include "common/io/Logger.h"
#include "agent/ContextCompressor.h"
#include <sstream>

namespace ea::agent {

ProgressiveMemoryStrategy::ProgressiveMemoryStrategy(ProgressiveMemoryConfig config)
    : config_(std::move(config)) {}

std::string ProgressiveMemoryStrategy::get_extraction_prompt() const {
    if (!config_.fact_extraction_prompt.empty()) {
        return config_.fact_extraction_prompt;
    }
    return "Extract key facts from this conversation turn. Return each fact as a separate line starting with \"- \".\n"
           "Focus on: user preferences, decisions made, important entities, constraints, and outcomes.\n"
           "Omit: greetings, acknowledgments, and routine exchanges.\n\n"
           "User: {user_input}\n"
           "Assistant: {assistant_output}";
}

std::string ProgressiveMemoryStrategy::get_summarization_prompt() const {
    if (!config_.summarization_prompt.empty()) {
        return config_.summarization_prompt;
    }
    return "Summarize the following conversation segment concisely, preserving key facts, decisions, and outcomes.\n"
           "Omit greetings and repetitions. Focus on information that would be needed in future turns.";
}

void ProgressiveMemoryStrategy::extract_facts(MemoryStrategyContext& ctx) {
    if (!config_.enable_fact_extraction) return;
    if (!ctx.provider || !ctx.memory) return;

    // Build extraction prompt
    std::string prompt = get_extraction_prompt();
    // Replace placeholders
    size_t pos;
    while ((pos = prompt.find("{user_input}")) != std::string::npos) {
        prompt.replace(pos, 12, ctx.user_input);
    }
    while ((pos = prompt.find("{assistant_output}")) != std::string::npos) {
        prompt.replace(pos, 18, ctx.assistant_output);
    }

    // Call LLM
    std::vector<Message> req = {
        {Role::System, "You are a fact extraction assistant. Extract only factual information.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = ctx.provider->chat(req, {}, "", opts);
    if (!response.ok()) {
        EA_WARN("Fact extraction failed: {}", response.error().message);
        return;
    }

    // Parse response: each line starting with "- " is a fact
    std::istringstream stream(response.value().content);
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        // Trim whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.pop_back();
        }

        if (line.size() > 2 && line[0] == '-' && line[1] == ' ') {
            std::string fact = line.substr(2);
            if (!fact.empty()) {
                auto store_result = ctx.memory->store(fact, "long_term", config_.long_term_importance);
                if (!store_result.ok()) {
                    EA_WARN("Failed to store extracted fact: {}", store_result.error().message);
                } else {
                    count++;
                }
            }
        }
    }

    EA_DEBUG("Extracted {} facts from turn", count);
}

void ProgressiveMemoryStrategy::summarize_old_messages(MemoryStrategyContext& ctx) {
    if (!config_.enable_auto_summarize) return;
    if (!ctx.provider || !ctx.memory) return;

    size_t max_working = static_cast<size_t>(config_.working_turns) * 2;
    if (ctx.history.size() <= max_working) return;

    // Find the boundary: system messages at front + non-system messages to summarize
    size_t sys_end = 0;
    while (sys_end < ctx.history.size() && ctx.history[sys_end].role == Role::System) {
        ++sys_end;
    }

    // Messages to summarize: from sys_end to (history.size() - max_working)
    size_t summarize_end = ctx.history.size() - max_working;
    if (summarize_end <= sys_end) return;  // Nothing to summarize beyond system messages

    // Collect messages to summarize
    std::vector<Message> to_summarize;
    for (size_t i = sys_end; i < summarize_end; ++i) {
        to_summarize.push_back(ctx.history[i]);
    }

    if (to_summarize.empty()) return;

    // LLM summarization
    std::string sum_prompt = get_summarization_prompt();
    std::string conv_text;
    for (const auto& msg : to_summarize) {
        const char* role_name = "unknown";
        switch (msg.role) {
            case Role::System:    role_name = "System"; break;
            case Role::User:      role_name = "User"; break;
            case Role::Assistant: role_name = "Assistant"; break;
            case Role::Tool:      role_name = "Tool"; break;
        }
        conv_text += role_name;
        conv_text += ": ";
        conv_text += msg.content;
        conv_text += "\n\n";
    }

    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, sum_prompt + "\n\n" + conv_text,
         std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = ctx.provider->chat(req, {}, "", opts);

    std::string summary;
    if (response.ok()) {
        summary = response.value().content;
    } else {
        EA_WARN("Summarization failed, using simple truncation: {}", response.error().message);
        summary = "[Earlier conversation history pruned to fit context window]";
    }

    // Store summary in short-term memory
    auto store_result = ctx.memory->store(summary, "short_term", 5);
    if (!store_result.ok()) {
        EA_WARN("Failed to store summary: {}", store_result.error().message);
    }

    // Remove summarized messages from history (in-place)
    // Keep: [0..sys_end) system messages + [summarize_end..end) recent messages
    // Insert: [Summary] breadcrumb after system messages

    Message breadcrumb;
    breadcrumb.role = Role::System;
    breadcrumb.content = "[Conversation Summary]\n" + summary;

    std::vector<Message> new_history;
    new_history.reserve(sys_end + 1 + (ctx.history.size() - summarize_end));

    for (size_t i = 0; i < sys_end; ++i) {
        new_history.push_back(std::move(ctx.history[i]));
    }
    new_history.push_back(std::move(breadcrumb));
    for (size_t i = summarize_end; i < ctx.history.size(); ++i) {
        new_history.push_back(std::move(ctx.history[i]));
    }

    ctx.history = std::move(new_history);

    EA_INFO("Summarized {} old messages into short-term memory ({} → {} messages in history)",
            to_summarize.size(), to_summarize.size() + ctx.history.size(), ctx.history.size());
}

void ProgressiveMemoryStrategy::evict_short_term(IMemory* memory) {
    if (!memory) return;

    // Count short-term entries
    auto list_result = memory->list(config_.short_term_max + 10, 0);
    if (!list_result.ok()) {
        EA_WARN("Failed to list short-term memories for eviction: {}", list_result.error().message);
        return;
    }

    // Filter to short_term category only
    std::vector<MemoryEntry> short_term_entries;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            short_term_entries.push_back(entry);
        }
    }

    if (static_cast<int>(short_term_entries.size()) <= config_.short_term_max) return;

    // Evict oldest entries (list returns in creation order)
    int to_evict = static_cast<int>(short_term_entries.size()) - config_.short_term_max;
    int evicted = 0;
    for (int i = 0; i < to_evict && i < static_cast<int>(short_term_entries.size()); ++i) {
        auto forget_result = memory->forget(short_term_entries[i].id);
        if (forget_result.ok() && forget_result.value()) {
            evicted++;
        }
    }

    if (evicted > 0) {
        EA_DEBUG("Evicted {} old short-term memory entries", evicted);
    }
}

std::string ProgressiveMemoryStrategy::build_memory_prompt(const std::vector<Message>& history,
                                                            IMemory* memory) {
    if (!memory) return {};

    std::ostringstream prompt;

    // 1. Query long-term memory using last user message
    std::string last_user_msg;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->role == Role::User) {
            last_user_msg = it->content;
            break;
        }
    }

    bool has_any = false;

    if (!last_user_msg.empty()) {
        auto lt_result = memory->recall(last_user_msg, 3);
        if (lt_result.ok() && !lt_result.value().empty()) {
            // Filter to long_term category
            std::vector<MemoryEntry> long_term;
            for (const auto& entry : lt_result.value()) {
                if (entry.category == "long_term") {
                    long_term.push_back(entry);
                }
            }
            if (!long_term.empty()) {
                has_any = true;
                prompt << "# Conversation Context\n## Key Facts\n";
                for (const auto& entry : long_term) {
                    prompt << "- " << entry.content << "\n";
                }
            }
        }
    }

    // 2. Query short-term memory
    auto st_result = memory->list(config_.short_term_max, 0);
    if (st_result.ok()) {
        std::vector<MemoryEntry> short_term;
        for (const auto& entry : st_result.value()) {
            if (entry.category == "short_term") {
                short_term.push_back(entry);
            }
        }
        if (!short_term.empty()) {
            if (!has_any) {
                prompt << "# Conversation Context\n";
                has_any = true;
            }
            prompt << "## Recent Summary\n";
            for (const auto& entry : short_term) {
                prompt << "- " << entry.content << "\n";
            }
        }
    }

    return has_any ? prompt.str() : std::string{};
}

void ProgressiveMemoryStrategy::on_turn_end(MemoryStrategyContext& ctx) {
    // Step 1: Extract key facts
    extract_facts(ctx);

    // Step 2: Summarize old messages into short-term
    summarize_old_messages(ctx);

    // Step 3: Evict excess short-term entries
    evict_short_term(ctx.memory);
}

}  // namespace ea::agent
```

- [ ] **Step 2: Update src/agent/CMakeLists.txt — add ProgressiveMemoryStrategy.cpp**

Add `ProgressiveMemoryStrategy.cpp` to the `add_library(ea-agent OBJECT ...)` source list, after `ContextCompressor.cpp`.

- [ ] **Step 3: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/agent/ProgressiveMemoryStrategy.cpp src/agent/CMakeLists.txt
git commit -m "feat: implement ProgressiveMemoryStrategy with extract/summarize/evict"
```

---

### Task 4: AgentLoop Integration

**Files:**
- Modify: `src/agent/AgentLoop.h` — add `IMemoryStrategy* strategy_` member + constructor param
- Modify: `src/agent/AgentLoop.cpp` — add strategy call sites

**Interfaces:**
- Consumes: `IMemoryStrategy` from Task 1, `MemoryStrategyContext` from Task 1
- Produces: AgentLoop with optional strategy support; `strategy_` pointer accessible for callers

- [ ] **Step 1: Modify AgentLoop.h**

Add `#include "IMemoryStrategy.h"` after the existing includes.

Add `IMemoryStrategy* strategy = nullptr` parameter to the AgentLoop constructor (after `ContextCompressor* compressor = nullptr`).

Add `IMemoryStrategy* strategy_;` private member (after `ContextCompressor* compressor_;`).

The constructor declaration becomes:
```cpp
AgentLoop(IProvider* provider,
          ToolRegistry* registry,
          IMemory* memory,
          Config config,
          OutputFn output,
          StreamFn stream_fn = nullptr,
          security::SecurityPolicy* policy = nullptr,
          security::IApprovalHandler* approval = nullptr,
          ContextCompressor* compressor = nullptr,
          IMemoryStrategy* strategy = nullptr);
```

The private member section adds:
```cpp
IMemoryStrategy* strategy_;
```

- [ ] **Step 2: Modify AgentLoop.cpp**

In the constructor initializer list, add `, strategy_(strategy)`.

In `build_system_prompt_once()`, after the line `system_prompt_ = build_system_prompt(ctx);`, add:
```cpp
// Inject memory strategy prompt if available
if (strategy_ && memory_) {
    auto mem_prompt = strategy_->build_memory_prompt(history_, memory_);
    if (!mem_prompt.empty()) {
        system_prompt_ += "\n" + mem_prompt;
    }
}
```

In `run()`, after the block that checks `ctx.should_stop` (the block ending with `return {};` around line 98), before the closing `}`, add the on_turn_end call:
```cpp
// After successful turn, let memory strategy process this turn
if (strategy_ && memory_ && provider_) {
    // Find last assistant output
    std::string last_output;
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->role == Role::Assistant) {
            last_output = it->content;
            break;
        }
    }
    MemoryStrategyContext mctx{history_, memory_, provider_, user_input, last_output};
    strategy_->on_turn_end(mctx);
}
```

Note: the `user_input` variable is the parameter of `run()`.

- [ ] **Step 3: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds (strategy_ defaults to nullptr, existing behavior unchanged)

- [ ] **Step 4: Run existing tests to verify no regression**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All existing tests pass

- [ ] **Step 5: Commit**

```bash
git add src/agent/AgentLoop.h src/agent/AgentLoop.cpp
git commit -m "feat: integrate IMemoryStrategy into AgentLoop"
```

---

### Task 5: Configuration Support

**Files:**
- Modify: `src/config/Config.h` — add MemoryStrategyConfig struct, add field to AppConfig
- Modify: `src/config/Config.cpp` — parse `[memory.strategy]` TOML section

**Interfaces:**
- Consumes: Existing `AppConfig`, `MemoryConfig` patterns
- Produces: `MemoryStrategyConfig` struct, `AppConfig.memory_strategy` field

- [ ] **Step 1: Add MemoryStrategyConfig to Config.h**

After the `MemoryConfig` struct, add:
```cpp
struct MemoryStrategyConfig {
    std::string type = "progressive";       // "progressive" | "none"
    int working_turns = 6;
    int short_term_max = 20;
    int long_term_importance = 8;
    bool enable_fact_extraction = true;
    bool enable_auto_summarize = true;
};
```

In `AppConfig`, add `MemoryStrategyConfig memory_strategy;` after `MemoryConfig memory;`.

- [ ] **Step 2: Parse [memory.strategy] in Config.cpp**

Inside the `if (data.contains("memory"))` block (after parsing `enable_fts5`), add:
```cpp
if (memory.contains("strategy")) {
    auto strategy = toml::find(memory, "strategy");
    cfg.memory_strategy.type = toml::find_or<std::string>(strategy, "type", cfg.memory_strategy.type);
    cfg.memory_strategy.working_turns = toml::find_or<int>(strategy, "working_turns", cfg.memory_strategy.working_turns);
    cfg.memory_strategy.short_term_max = toml::find_or<int>(strategy, "short_term_max", cfg.memory_strategy.short_term_max);
    cfg.memory_strategy.long_term_importance = toml::find_or<int>(strategy, "long_term_importance", cfg.memory_strategy.long_term_importance);
    cfg.memory_strategy.enable_fact_extraction = toml::find_or<bool>(strategy, "enable_fact_extraction", cfg.memory_strategy.enable_fact_extraction);
    cfg.memory_strategy.enable_auto_summarize = toml::find_or<bool>(strategy, "enable_auto_summarize", cfg.memory_strategy.enable_auto_summarize);
}
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp
git commit -m "feat: add MemoryStrategyConfig and TOML parsing"
```

---

### Task 6: main.cpp Wiring

**Files:**
- Modify: `src/main.cpp` — create ProgressiveMemoryStrategy and pass to AgentLoop

**Interfaces:**
- Consumes: `ProgressiveMemoryStrategy`, `ProgressiveMemoryConfig` from Task 3, `MemoryStrategyConfig` from Task 5, `AgentLoop` constructor from Task 4

- [ ] **Step 1: Add include and create strategy in main.cpp**

Add after the existing `#include "agent/ContextCompressor.h"`:
```cpp
#include "agent/ProgressiveMemoryStrategy.h"
```

After the context compressor creation block (section 5.6), add section 5.7:
```cpp
// 5.7. Create memory strategy
std::unique_ptr<ea::agent::IMemoryStrategy> strategy;
if (cfg.memory_strategy.type == "progressive") {
    ea::agent::ProgressiveMemoryConfig strat_cfg;
    strat_cfg.working_turns = cfg.memory_strategy.working_turns;
    strat_cfg.short_term_max = cfg.memory_strategy.short_term_max;
    strat_cfg.long_term_importance = cfg.memory_strategy.long_term_importance;
    strat_cfg.enable_fact_extraction = cfg.memory_strategy.enable_fact_extraction;
    strat_cfg.enable_auto_summarize = cfg.memory_strategy.enable_auto_summarize;
    strategy = std::make_unique<ea::agent::ProgressiveMemoryStrategy>(strat_cfg);
}
// type == "none" → strategy stays nullptr → NullMemoryStrategy behavior (no-op)
```

Update the AgentLoop construction to pass `strategy.get()` as the last argument:
```cpp
ea::agent::AgentLoop loop(
    provider.get(), &registry, memory.get(),
    ea::agent::AgentLoop::Config{
        cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream
    },
    [](const std::string& text) { std::cout << text << std::endl; },
    stream_fn,
    security.get(),
    approval.get(),
    compressor.get(),
    strategy.get()
);
```

- [ ] **Step 2: Verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 3: Run all existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All existing tests pass

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire ProgressiveMemoryStrategy in main.cpp"
```

---

### Task 7: Tests

**Files:**
- Create: `tests/test_memory_strategy.cpp`
- Modify: `tests/CMakeLists.txt` — add test file

**Interfaces:**
- Consumes: All classes from Tasks 1-4: `IMemoryStrategy`, `NullMemoryStrategy`, `ProgressiveMemoryStrategy`, `ProgressiveMemoryConfig`, `MemoryStrategyContext`, `AgentLoop`, `InMemoryBackend`
- Produces: Comprehensive test coverage for memory strategy

- [ ] **Step 1: Create test file**

```cpp
// tests/test_memory_strategy.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/IMemoryStrategy.h"
#include "agent/ProgressiveMemoryStrategy.h"
#include "agent/AgentLoop.h"
#include "memory/InMemoryBackend.h"
#include "core/IProvider.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// --- Mock provider that returns controlled responses ---

class StrategyTestProvider : public IProvider {
public:
    std::string name() const override { return "strategy-test"; }
    std::vector<std::string> list_models() const override { return {"test-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        chat_count_++;
        last_messages_ = msgs;

        if (fail_next_) {
            fail_next_ = false;
            return Error::net("provider error");
        }

        // Return different responses based on context
        // If the user message contains "extract" → return fact-like lines
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
            if (it->role == Role::User) {
                if (it->content.find("Extract key facts") != std::string::npos) {
                    LLMResponse resp;
                    resp.content = "- User prefers dark mode\n- Project uses CMake\n";
                    resp.stop_reason = "stop";
                    return resp;
                }
                if (it->content.find("Summarize") != std::string::npos ||
                    it->content.find("summarizer") != std::string::npos) {
                    LLMResponse resp;
                    resp.content = "User discussed testing strategies and CMake configuration.";
                    resp.stop_reason = "stop";
                    return resp;
                }
                break;
            }
        }

        LLMResponse resp;
        resp.content = "Default response";
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int chat_count() const { return chat_count_; }
    void set_fail_next(bool f) { fail_next_ = f; }
    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    int chat_count_ = 0;
    bool fail_next_ = false;
    std::vector<Message> last_messages_;
};

// --- NullMemoryStrategy tests ---

TEST_CASE("NullMemoryStrategy returns empty prompt", "[memory][strategy]") {
    NullMemoryStrategy strategy;
    std::vector<Message> history;
    REQUIRE(strategy.build_memory_prompt(history, nullptr).empty());
}

TEST_CASE("NullMemoryStrategy on_turn_end is no-op", "[memory][strategy]") {
    NullMemoryStrategy strategy;
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();
    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(history.empty());
    REQUIRE(strategy.is_active() == false);
}

// --- ProgressiveMemoryConfig defaults ---

TEST_CASE("ProgressiveMemoryConfig default values", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    REQUIRE(config.working_turns == 6);
    REQUIRE(config.short_term_max == 20);
    REQUIRE(config.long_term_importance == 8);
    REQUIRE(config.enable_fact_extraction == true);
    REQUIRE(config.enable_auto_summarize == true);
    REQUIRE(config.fact_extraction_prompt.empty());
    REQUIRE(config.summarization_prompt.empty());
}

// --- ProgressiveMemoryStrategy.is_active ---

TEST_CASE("ProgressiveMemoryStrategy is active", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    REQUIRE(strategy.is_active() == true);
}

// --- build_memory_prompt tests ---

TEST_CASE("build_memory_prompt returns empty with no memories", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;
    std::vector<Message> history = {
        {Role::User, "hello", std::nullopt, std::nullopt, std::nullopt}
    };

    REQUIRE(strategy.build_memory_prompt(history, &memory).empty());
}

TEST_CASE("build_memory_prompt returns empty with null memory", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    std::vector<Message> history = {
        {Role::User, "hello", std::nullopt, std::nullopt, std::nullopt}
    };

    REQUIRE(strategy.build_memory_prompt(history, nullptr).empty());
}

TEST_CASE("build_memory_prompt includes long-term facts", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;

    memory.store("User prefers dark mode", "long_term", 8);
    memory.store("Project uses CMake", "long_term", 8);

    std::vector<Message> history = {
        {Role::User, "dark mode", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, &memory);
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Key Facts") != std::string::npos);
    REQUIRE(prompt.find("dark mode") != std::string::npos);
}

TEST_CASE("build_memory_prompt includes short-term summaries", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;

    memory.store("Previous conversation about testing", "short_term", 5);

    std::vector<Message> history = {
        {Role::User, "continue", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, &memory);
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Recent Summary") != std::string::npos);
    REQUIRE(prompt.find("testing") != std::string::npos);
}

TEST_CASE("build_memory_prompt includes both long-term and short-term", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;

    memory.store("User prefers dark mode", "long_term", 8);
    memory.store("Previous discussion about CMake", "short_term", 5);

    std::vector<Message> history = {
        {Role::User, "dark mode cmake", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, &memory);
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("Key Facts") != std::string::npos);
    REQUIRE(prompt.find("Recent Summary") != std::string::npos);
}

TEST_CASE("build_memory_prompt filters by category", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;

    // Store a "core" category entry — should NOT appear in memory prompt
    memory.store("Some core memory", "core", 5);
    memory.store("A long-term fact", "long_term", 8);

    std::vector<Message> history = {
        {Role::User, "fact", std::nullopt, std::nullopt, std::nullopt}
    };

    std::string prompt = strategy.build_memory_prompt(history, &memory);
    REQUIRE(!prompt.empty());
    REQUIRE(prompt.find("long-term fact") != std::string::npos);
    // "core" entries should not be in the structured prompt sections
}

// --- on_turn_end: fact extraction ---

TEST_CASE("on_turn_end extracts facts from conversation", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "I prefer dark mode";
    std::string output = "Noted, you prefer dark mode";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Check that facts were stored in long-term memory
    auto list_result = memory.list(50, 0);
    REQUIRE(list_result.ok());
    bool found_long_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "long_term") {
            found_long_term = true;
            break;
        }
    }
    REQUIRE(found_long_term);
    REQUIRE(provider->chat_count() >= 1);
}

TEST_CASE("on_turn_end skips fact extraction when disabled", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(provider->chat_count() == 0);
}

TEST_CASE("on_turn_end gracefully handles LLM failure in extraction", "[memory][strategy]") {
    ProgressiveMemoryStrategy strategy;
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();
    provider->set_fail_next(true);

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Should not crash, no long-term entries stored
    auto list_result = memory.list(50, 0);
    REQUIRE(list_result.ok());
    bool found_long_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "long_term") {
            found_long_term = true;
        }
    }
    REQUIRE_FALSE(found_long_term);
}

// --- on_turn_end: summarization ---

TEST_CASE("on_turn_end summarizes old messages when history exceeds working_turns", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 2;  // Keep only last 2 turns = 4 messages
    config.enable_fact_extraction = false;  // Focus on summarization
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    // Build history with more than 4 messages (2 turns)
    std::vector<Message> history;
    for (int i = 0; i < 5; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    std::string input = "Q4";
    std::string output = "A4";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // History should be shorter than 10 messages
    REQUIRE(history.size() < 10);

    // Short-term memory should have a summary
    auto list_result = memory.list(50, 0);
    REQUIRE(list_result.ok());
    bool found_short_term = false;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            found_short_term = true;
            break;
        }
    }
    REQUIRE(found_short_term);
}

TEST_CASE("on_turn_end does not summarize when history is within working_turns", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 10;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history = {
        {Role::User, "Q0", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "A0", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Q1", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "A1", std::nullopt, std::nullopt, std::nullopt}
    };

    size_t original_size = history.size();
    std::string input = "Q1";
    std::string output = "A1";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    REQUIRE(history.size() == original_size);
    REQUIRE(provider->chat_count() == 0);
}

TEST_CASE("on_turn_end skips summarization when disabled", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 1;
    config.enable_auto_summarize = false;
    config.enable_fact_extraction = false;
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    for (int i = 0; i < 6; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    size_t original_size = history.size();
    std::string input = "Q5";
    std::string output = "A5";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // History should be unchanged
    REQUIRE(history.size() == original_size);
    REQUIRE(provider->chat_count() == 0);
}

// --- on_turn_end: short-term eviction ---

TEST_CASE("on_turn_end evicts excess short-term entries", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.short_term_max = 3;
    config.enable_fact_extraction = false;
    config.enable_auto_summarize = false;
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;

    // Pre-fill with more than short_term_max entries
    for (int i = 0; i < 5; ++i) {
        memory.store("Summary " + std::to_string(i), "short_term", 5);
    }

    std::vector<Message> history;
    std::string input = "hello";
    std::string output = "world";
    auto provider = std::make_shared<StrategyTestProvider>();

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Count remaining short_term entries
    auto list_result = memory.list(50, 0);
    REQUIRE(list_result.ok());
    int short_term_count = 0;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            short_term_count++;
        }
    }
    REQUIRE(short_term_count <= config.short_term_max);
}

// --- Custom prompts ---

TEST_CASE("ProgressiveMemoryStrategy uses custom extraction prompt", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.fact_extraction_prompt = "CUSTOM_EXTRACT: {user_input} | {assistant_output}";
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    std::string input = "test input";
    std::string output = "test output";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Verify the custom prompt was used
    REQUIRE(provider->chat_count() >= 1);
    bool found_custom = false;
    for (const auto& msg : provider->last_messages()) {
        if (msg.content.find("CUSTOM_EXTRACT") != std::string::npos) {
            found_custom = true;
            break;
        }
    }
    REQUIRE(found_custom);
}

TEST_CASE("ProgressiveMemoryStrategy uses custom summarization prompt", "[memory][strategy]") {
    ProgressiveMemoryConfig config;
    config.working_turns = 1;
    config.enable_fact_extraction = false;
    config.summarization_prompt = "CUSTOM_SUMMARIZE";
    ProgressiveMemoryStrategy strategy(config);
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    std::vector<Message> history;
    for (int i = 0; i < 4; ++i) {
        history.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        history.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    std::string input = "Q3";
    std::string output = "A3";

    MemoryStrategyContext ctx{history, &memory, provider.get(), input, output};
    strategy.on_turn_end(ctx);

    // Verify the custom prompt was used
    REQUIRE(provider->chat_count() >= 1);
    bool found_custom = false;
    for (const auto& msg : provider->last_messages()) {
        if (msg.content.find("CUSTOM_SUMMARIZE") != std::string::npos) {
            found_custom = true;
            break;
        }
    }
    REQUIRE(found_custom);
}

// --- Integration: AgentLoop + ProgressiveMemoryStrategy ---

TEST_CASE("AgentLoop with strategy processes turns", "[agent][memory][strategy]") {
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();

    ProgressiveMemoryConfig strat_config;
    strat_config.enable_fact_extraction = true;
    strat_config.enable_auto_summarize = false;  // Short history, no need
    auto strategy = std::make_unique<ProgressiveMemoryStrategy>(strat_config);

    ea::tool::ToolRegistry registry;

    AgentLoop loop(
        provider.get(), &registry, &memory,
        AgentLoop::Config{5, 1024, 100, true, false},
        [](const std::string&) {},
        nullptr,  // no stream
        nullptr,  // no security
        nullptr,  // no approval
        nullptr,  // no compressor
        strategy.get()
    );

    auto result = loop.run("Tell me about dark mode");
    REQUIRE(result.ok());

    // After a turn, memory should have been updated
    auto mem_list = memory.list(50, 0);
    REQUIRE(mem_list.ok());
    // At least some long-term facts should have been stored
    bool found = false;
    for (const auto& entry : mem_list.value()) {
        if (entry.category == "long_term") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("AgentLoop without strategy works as before", "[agent][memory]") {
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();
    ea::tool::ToolRegistry registry;

    AgentLoop loop(
        provider.get(), &registry, &memory,
        AgentLoop::Config{5, 1024, 100, true, false},
        [](const std::string&) {},
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr  // no strategy
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());
    REQUIRE(loop.history().size() >= 2);
}

TEST_CASE("AgentLoop with NullMemoryStrategy behaves same as no strategy", "[agent][memory][strategy]") {
    InMemoryBackend memory;
    auto provider = std::make_shared<StrategyTestProvider>();
    ea::tool::ToolRegistry registry;
    NullMemoryStrategy null_strategy;

    AgentLoop loop(
        provider.get(), &registry, &memory,
        AgentLoop::Config{5, 1024, 100, true, false},
        [](const std::string&) {},
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &null_strategy
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());

    // Memory should be unchanged — NullMemoryStrategy does nothing
    auto mem_list = memory.list(50, 0);
    REQUIRE(mem_list.ok());
    REQUIRE(mem_list.value().empty());
}
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

Add `test_memory_strategy.cpp` to the `add_executable(ea-tests ...)` source list, after `test_http_server.cpp`.

- [ ] **Step 3: Build and run the new tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[strategy]" 2>&1`
Expected: All strategy tests pass

- [ ] **Step 4: Run all tests to verify no regression**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All tests pass

- [ ] **Step 5: Commit**

```bash
git add tests/test_memory_strategy.cpp tests/CMakeLists.txt
git commit -m "test: add comprehensive memory strategy tests"
```

---

### Task 8: Final Verification

**Files:**
- No new files — full build + test verification

- [ ] **Step 1: Clean rebuild**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds with 0 errors

- [ ] **Step 2: Run all tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All tests pass (existing 348 + new strategy tests)

- [ ] **Step 3: Verify test counts**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests --reporter compact 2>&1 | tail -3`
Expected: All tests pass, increased assertion count from baseline

- [ ] **Step 4: Commit any remaining fixes**

```bash
git add -A
git commit -m "fix: final verification adjustments for Phase 4D"
```
(Only if there are changes; skip if clean)
