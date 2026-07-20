# Phase 4E: Token Budget & Cost Tracking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add token usage tracking and cost estimation to the agent, with session/global accumulation, SQLite persistence, budget warnings, CLI commands, and Server API endpoints.

**Architecture:** BudgetTracker provider decorator intercepts all LLM calls, extracts Usage data, accumulates at session and global levels, and calculates costs via a configurable model pricing table. Usage records persist to a separate SQLite file (usage.db) via IUsageStore/SqliteUsageStore. BudgetTracker wraps the existing provider chain in main.cpp and SessionManager.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, sqlite3, Catch2, httplib

## Global Constraints

- Error construction: use `Error` factory methods (`Error::db()`, `Error::not_found()`, `Error::parse()`, `Error::invalid_arg()`)
- Compile flags: use `ea_target_compile_options(target)` for project targets
- Object libraries: each module is a CMake `OBJECT` library
- Test tags: `[budget]` for unit tests, `[agent] [budget]` for integration tests
- Namespace: `ea::budget`
- Budget tracking failures never block the conversation flow
- Thread safety: mutex-protected counters, `gmtime_r()` for timestamps, mutex-protected RNG for ID generation
- httplib route ordering: more specific regex patterns registered before less specific ones
- `std::stoi` must be wrapped in try-catch with bounds validation
- Config.h includes `budget/Types.h` and uses `budget::BudgetConfig` directly (no duplicate config struct)
- Provider chain: `AgentLoop → BudgetTracker → ReliableProvider → OpenAIProvider`

---

### Task 1: Types.h + IUsageStore Interface + Module Scaffold

**Files:**
- Create: `src/budget/Types.h`
- Create: `src/budget/IUsageStore.h`
- Create: `src/budget/CMakeLists.txt`
- Modify: `CMakeLists.txt` — add `add_subdirectory(src/budget)` after `src/conversation`, add `$<TARGET_OBJECTS:ea-budget>` to static lib
- Modify: `tests/CMakeLists.txt` — add `ea-budget` to link libraries
- Modify: `src/config/Config.h` — add `#include "budget/Types.h"` and `budget::BudgetConfig budget;` field
- Modify: `src/config/Config.cpp` — add `[budget]` TOML parsing with `[[budget.pricing]]` array

**Interfaces:**
- Produces: `ea::budget::UsageSnapshot` (with `operator+=`, `operator+`, `total_tokens()`), `ea::budget::CostSnapshot` (with `operator+=`, `total()`), `ea::budget::ModelPricing` (with `calculate()`), `ea::budget::BudgetConfig`, `ea::budget::UsageRecord`, `ea::budget::IUsageStore`

- [ ] **Step 1: Create `src/budget/Types.h`**

```cpp
#pragma once
#include <string>
#include <vector>

namespace ea::budget {

struct UsageSnapshot {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;

    UsageSnapshot& operator+=(const UsageSnapshot& other) {
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
        cache_read_tokens += other.cache_read_tokens;
        cache_write_tokens += other.cache_write_tokens;
        return *this;
    }

    UsageSnapshot operator+(const UsageSnapshot& other) const {
        UsageSnapshot result = *this;
        result += other;
        return result;
    }

    int total_tokens() const {
        return input_tokens + output_tokens + cache_read_tokens + cache_write_tokens;
    }
};

struct CostSnapshot {
    double input_cost = 0.0;
    double output_cost = 0.0;
    double cache_read_cost = 0.0;
    double cache_write_cost = 0.0;

    CostSnapshot& operator+=(const CostSnapshot& other) {
        input_cost += other.input_cost;
        output_cost += other.output_cost;
        cache_read_cost += other.cache_read_cost;
        cache_write_cost += other.cache_write_cost;
        return *this;
    }

    double total() const {
        return input_cost + output_cost + cache_read_cost + cache_write_cost;
    }
};

struct ModelPricing {
    std::string model_id;
    double input_per_mtok = 0.0;
    double output_per_mtok = 0.0;
    double cache_read_per_mtok = 0.0;
    double cache_write_per_mtok = 0.0;

    CostSnapshot calculate(const UsageSnapshot& usage) const {
        CostSnapshot cost;
        cost.input_cost = usage.input_tokens * input_per_mtok / 1000000.0;
        cost.output_cost = usage.output_tokens * output_per_mtok / 1000000.0;
        cost.cache_read_cost = usage.cache_read_tokens * cache_read_per_mtok / 1000000.0;
        cost.cache_write_cost = usage.cache_write_tokens * cache_write_per_mtok / 1000000.0;
        return cost;
    }
};

struct BudgetConfig {
    std::string path;
    int warn_input_tokens = 100000;
    int warn_output_tokens = 50000;
    double warn_cost_usd = 1.0;
    int max_input_tokens = 0;
    int max_output_tokens = 0;
    double max_cost_usd = 0.0;
    std::vector<ModelPricing> pricing;
};

}  // namespace ea::budget
```

- [ ] **Step 2: Create `src/budget/IUsageStore.h`**

```cpp
#pragma once
#include "Types.h"
#include "common/base/Result.h"
#include <string>
#include <vector>

namespace ea::budget {

struct UsageRecord {
    std::string id;
    std::string session_id;
    std::string model;
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;
    double cost_usd = 0.0;
    std::string timestamp;
};

class IUsageStore {
public:
    virtual ~IUsageStore() = default;
    virtual Result<std::string> record(const UsageRecord& rec) = 0;
    virtual Result<std::vector<UsageRecord>> query(
        const std::string& session_id = "",
        int limit = 100, int offset = 0) = 0;
    virtual Result<UsageSnapshot> total_usage(
        const std::string& session_id = "") = 0;
    virtual Result<CostSnapshot> total_cost(
        const std::string& session_id = "") = 0;
    virtual Result<bool> clear(
        const std::string& session_id = "") = 0;
    virtual Result<void> open() = 0;
    virtual Result<void> close() = 0;
};

}  // namespace ea::budget
```

- [ ] **Step 3: Create `src/budget/CMakeLists.txt`**

```cmake
add_library(ea-budget OBJECT
    SqliteUsageStore.cpp
    BudgetTracker.cpp
)
ea_target_compile_options(ea-budget)
target_include_directories(ea-budget PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-budget PUBLIC ea-core ea-common sqlite3)
```

Note: SqliteUsageStore.cpp and BudgetTracker.cpp don't exist yet — create empty stub files so CMake doesn't error. The next tasks will fill them in.

Create `src/budget/SqliteUsageStore.cpp` as empty stub:
```cpp
// Stub — implemented in Task 2
```

Create `src/budget/BudgetTracker.cpp` as empty stub:
```cpp
// Stub — implemented in Task 3
```

- [ ] **Step 4: Modify root `CMakeLists.txt`**

Add after line 94 (`add_subdirectory(src/conversation)`):
```cmake
add_subdirectory(src/budget)
```

Add `$<TARGET_OBJECTS:ea-budget>` after `$<TARGET_OBJECTS:ea-conversation>` in the `embedded-agent-core` static library (line 146).

- [ ] **Step 5: Modify `tests/CMakeLists.txt`**

Add `ea-budget` to `target_link_libraries` after `ea-conversation`.

Add two test source files (empty stubs for now):
```
test_budget_tracker.cpp
test_usage_store.cpp
```

Create `tests/test_budget_tracker.cpp` as:
```cpp
// Stub — implemented in Task 3
```

Create `tests/test_usage_store.cpp` as:
```cpp
// Stub — implemented in Task 2
```

- [ ] **Step 6: Modify `src/config/Config.h`**

Add `#include "budget/Types.h"` after `#include "conversation/IConversationStore.h"` equivalent.

Add `budget::BudgetConfig budget;` field in `AppConfig` after `ConversationConfig conversation;`.

- [ ] **Step 7: Modify `src/config/Config.cpp`**

Add `[budget]` TOML section parsing after the `[conversation]` section. Parse `path`, `warn_input_tokens`, `warn_output_tokens`, `warn_cost_usd`, `max_input_tokens`, `max_output_tokens`, `max_cost_usd`. Parse `[[budget.pricing]]` array with `model_id`, `input_per_mtok`, `output_per_mtok`, `cache_read_per_mtok`, `cache_write_per_mtok`.

```cpp
if (data.contains("budget")) {
    auto budget = toml::find(data, "budget");
    cfg.budget.path = toml::find_or<std::string>(budget, "path", cfg.budget.path);
    cfg.budget.warn_input_tokens = toml::find_or<int>(budget, "warn_input_tokens", cfg.budget.warn_input_tokens);
    cfg.budget.warn_output_tokens = toml::find_or<int>(budget, "warn_output_tokens", cfg.budget.warn_output_tokens);
    cfg.budget.warn_cost_usd = toml::find_or<double>(budget, "warn_cost_usd", cfg.budget.warn_cost_usd);
    cfg.budget.max_input_tokens = toml::find_or<int>(budget, "max_input_tokens", cfg.budget.max_input_tokens);
    cfg.budget.max_output_tokens = toml::find_or<int>(budget, "max_output_tokens", cfg.budget.max_output_tokens);
    cfg.budget.max_cost_usd = toml::find_or<double>(budget, "max_cost_usd", cfg.budget.max_cost_usd);
    if (budget.contains("pricing")) {
        auto pricings = toml::find<std::vector<toml::value>>(budget, "pricing");
        for (const auto& pv : pricings) {
            budget::ModelPricing mp;
            mp.model_id = toml::find<std::string>(pv, "model_id");
            mp.input_per_mtok = toml::find_or<double>(pv, "input_per_mtok", 0.0);
            mp.output_per_mtok = toml::find_or<double>(pv, "output_per_mtok", 0.0);
            mp.cache_read_per_mtok = toml::find_or<double>(pv, "cache_read_per_mtok", 0.0);
            mp.cache_write_per_mtok = toml::find_or<double>(pv, "cache_write_per_mtok", 0.0);
            cfg.budget.pricing.push_back(std::move(mp));
        }
    }
}
```

- [ ] **Step 8: Build and verify**

Run: `cd build && cmake --build . -j$(nproc)`
Expected: Clean build, no errors.

- [ ] **Step 9: Commit**

```bash
git add src/budget/ CMakeLists.txt tests/CMakeLists.txt src/config/Config.h src/config/Config.cpp tests/test_budget_tracker.cpp tests/test_usage_store.cpp
git commit -m "feat: add budget module scaffold with Types.h, IUsageStore interface, and config"
```

---

### Task 2: SqliteUsageStore Implementation + Unit Tests

**Files:**
- Create: `src/budget/SqliteUsageStore.h`
- Modify: `src/budget/SqliteUsageStore.cpp` — replace stub with full implementation
- Modify: `tests/test_usage_store.cpp` — replace stub with 10 unit tests

**Interfaces:**
- Consumes: `ea::budget::IUsageStore`, `ea::budget::UsageRecord`, `ea::budget::UsageSnapshot`, `ea::budget::CostSnapshot`, `ea::Error` factory methods
- Produces: `ea::budget::SqliteUsageStore` class with `Config{path, enable_wal}`

- [ ] **Step 1: Create `src/budget/SqliteUsageStore.h`**

```cpp
#pragma once
#include "IUsageStore.h"
#include <sqlite3.h>
#include <string>
#include <mutex>
#include <random>

namespace ea::budget {

class SqliteUsageStore : public IUsageStore {
public:
    struct Config {
        std::string path;
        bool enable_wal = true;
    };

    explicit SqliteUsageStore(Config config);
    ~SqliteUsageStore() override;

    SqliteUsageStore(const SqliteUsageStore&) = delete;
    SqliteUsageStore& operator=(const SqliteUsageStore&) = delete;

    Result<std::string> record(const UsageRecord& rec) override;
    Result<std::vector<UsageRecord>> query(
        const std::string& session_id = "",
        int limit = 100, int offset = 0) override;
    Result<UsageSnapshot> total_usage(
        const std::string& session_id = "") override;
    Result<CostSnapshot> total_cost(
        const std::string& session_id = "") override;
    Result<bool> clear(const std::string& session_id = "") override;
    Result<void> open() override;
    Result<void> close() override;

private:
    Result<void> create_tables();
    static std::string generate_id();
    static std::string current_iso8601();

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
    static std::mutex id_mutex_;
    static std::mt19937 rng_;
};

}  // namespace ea::budget
```

- [ ] **Step 2: Implement `src/budget/SqliteUsageStore.cpp`**

Follow the same patterns as `SqliteConversationStore`:

- `generate_id()`: "usage_" + 4 hex bytes (8 hex chars), mutex-protected mt19937
- `current_iso8601()`: `gmtime_r()` + millisecond precision
- `create_tables()`: CREATE TABLE usage_records + two indexes
- `open()`: sqlite3_open, pragma journal_mode=WAL, create_tables()
- `close()`: sqlite3_close
- `record()`: INSERT into usage_records, return generated ID
- `query()`: SELECT with optional session_id filter + LIMIT/OFFSET
- `total_usage()`: SELECT SUM(input_tokens), SUM(output_tokens), etc.
- `total_cost()`: SELECT SUM(cost_usd) — if session_id filter, add WHERE clause
- `clear()`: DELETE with optional session_id filter, return whether any rows deleted

- [ ] **Step 3: Write `tests/test_usage_store.cpp`**

10 unit tests with `[budget]` tag:

1. `record returns valid ID` — create store, record one entry, ID starts with "usage_"
2. `record and query` — record 3 entries, query all, verify count
3. `query by session_id` — record entries with different session_ids, query by one
4. `query with limit/offset` — record 5 entries, query limit=2 offset=1
5. `total_usage aggregation` — record 3 entries, total_usage matches sum
6. `total_cost aggregation` — record 3 entries with different costs, total_cost matches sum
7. `total_usage by session_id` — record entries for two sessions, filter by one
8. `clear by session_id` — clear one session, verify others remain
9. `clear all` — clear with empty session_id, verify empty
10. `query empty store` — query on empty store returns empty vector

Use a `TempUsageStore` fixture (like `TempConvStore` in conversation tests) that creates a temp DB and auto-cleans.

- [ ] **Step 4: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[budget]"`
Expected: All budget tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/budget/SqliteUsageStore.h src/budget/SqliteUsageStore.cpp tests/test_usage_store.cpp
git commit -m "feat: implement SqliteUsageStore with 10 unit tests"
```

---

### Task 3: BudgetTracker Decorator + Unit Tests

**Files:**
- Create: `src/budget/BudgetTracker.h`
- Modify: `src/budget/BudgetTracker.cpp` — replace stub with full implementation
- Modify: `tests/test_budget_tracker.cpp` — replace stub with 10 unit tests

**Interfaces:**
- Consumes: `ea::IProvider`, `ea::LLMResponse`, `ea::StreamChunk`, `ea::Usage`, `ea::budget::BudgetConfig`, `ea::budget::ModelPricing`, `ea::budget::IUsageStore`
- Produces: `ea::budget::BudgetTracker` (IProvider decorator with query/check/persistence methods)

- [ ] **Step 1: Create `src/budget/BudgetTracker.h`**

As specified in the design spec — full class declaration with:
- Constructor: `BudgetTracker(std::shared_ptr<IProvider> inner, BudgetConfig config)`
- IProvider overrides: `name()`, `list_models()`, `capabilities()`, `chat()`, `stream_chat()`
- Query: `session_usage()`, `session_cost()`, `global_usage()`, `global_cost()`
- Reset: `reset_session()`, `reset_global()`
- Budget: `is_over_warn()`, `is_over_limit()`
- Persistence: `set_store()`, `set_session_id()`, `flush()`
- Private: `record_usage()`, `find_pricing()`, `calculate_cost()`, `check_budget()`

- [ ] **Step 2: Implement `src/budget/BudgetTracker.cpp`**

Key implementations:

**`chat()`**: Call `inner_->chat()`, if ok extract `result.value().usage`, call `record_usage(usage, model)`, return result.

**`stream_chat()`**: Wrap `on_chunk` callback to capture usage from `StreamChunk`. If `chunk.type == Done && chunk.usage` has value, record usage. Forward all chunks to original callback.

**`record_usage()`**: Lock mutex, convert `Usage` → `UsageSnapshot`, add to session/global accumulators, calculate cost, add to session/global cost, if store_ set write UsageRecord, call `check_budget()`.

**`find_pricing()`**: Iterate `config_.pricing`, check if model starts with `mp.model_id` (prefix match). First match wins. Return nullptr if no match.

**`check_budget()`**: Compare session/global usage and cost against `warn_*` / `max_*` thresholds. If exceeded, log EA_WARN for warn, EA_ERROR for limit.

**`is_over_warn()`**: Return true if session cost > `config_.warn_cost_usd` OR session input > `config_.warn_input_tokens` OR session output > `config_.warn_output_tokens`.

**`is_over_limit()`**: Return true if `max_*` > 0 and session exceeds those limits.

**`flush()`**: No-op for now (records are written immediately in `record_usage()`). Placeholder for future batched writes.

- [ ] **Step 3: Write `tests/test_budget_tracker.cpp`**

10 unit tests with `[budget]` tag:

1. `chat intercepts usage` — MockProvider returns LLMResponse with usage=100/50, verify session_usage() returns {100, 50, 0, 0}
2. `stream_chat intercepts usage` — MockProvider with stream_chat that emits Done chunk with usage
3. `session and global accumulation` — Two chat() calls, verify both session and global accumulate
4. `reset_session` — Chat, reset_session, verify session is zero but global keeps
5. `model pricing calculate` — Verify cost calculation with known rates
6. `model prefix matching` — model "gpt-4o-2024-08-06" matches pricing for "gpt-4o"
7. `unmatched model zero cost` — Model not in pricing table, cost is $0
8. `budget warn detection` — Set warn_cost_usd=0.001, one chat with cost > threshold, is_over_warn() returns true
9. `budget limit detection` — Set max_cost_usd=0.001, one chat exceeds, is_over_limit() returns true
10. `with no store` — BudgetTracker without IUsageStore, in-memory only, still works

Create a `MockProvider` in the test file that returns predetermined LLMResponse with usage data.

- [ ] **Step 4: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[budget]"`
Expected: All budget tests pass (20 total from Tasks 2+3).

- [ ] **Step 5: Commit**

```bash
git add src/budget/BudgetTracker.h src/budget/BudgetTracker.cpp tests/test_budget_tracker.cpp
git commit -m "feat: implement BudgetTracker decorator with 10 unit tests"
```

---

### Task 4: AgentLoop Integration (AgentEvent + TurnContext)

**Files:**
- Modify: `src/agent/AgentEvent.h` — add `turn_usage` and `turn_cost` fields
- Modify: `src/agent/TurnContext.h` — add `budget_tracker` pointer
- Modify: `src/agent/AgentLoop.h` — add `budget::BudgetTracker*` member, constructor param, getter
- Modify: `src/agent/AgentLoop.cpp` — populate TurnContext.budget_tracker, emit usage in TurnEnd
- Modify: `src/agent/CMakeLists.txt` — add `ea-budget` to link libraries

**Interfaces:**
- Consumes: `ea::budget::BudgetTracker` (query methods)
- Produces: `AgentEvent.turn_usage`, `AgentEvent.turn_cost`, `TurnContext.budget_tracker`, `AgentLoop::budget_tracker()` getter

- [ ] **Step 1: Modify `src/agent/AgentEvent.h`**

Add after existing includes:
```cpp
#include "budget/Types.h"
```

Add fields to `AgentEvent`:
```cpp
budget::UsageSnapshot turn_usage;     // TurnEnd
budget::CostSnapshot turn_cost;       // TurnEnd
```

- [ ] **Step 2: Modify `src/agent/TurnContext.h`**

Add forward declaration and field:
```cpp
namespace ea::budget { class BudgetTracker; }

struct TurnContext {
    // ... existing fields ...
    budget::BudgetTracker* budget_tracker = nullptr;
};
```

- [ ] **Step 3: Modify `src/agent/AgentLoop.h`**

Add include:
```cpp
#include "budget/BudgetTracker.h"
```

Add 12th constructor parameter after `conv_store`:
```cpp
budget::BudgetTracker* budget_tracker = nullptr
```

Add member:
```cpp
budget::BudgetTracker* budget_tracker_;
```

Add getter:
```cpp
budget::BudgetTracker* budget_tracker() const { return budget_tracker_; }
```

- [ ] **Step 4: Modify `src/agent/AgentLoop.cpp`**

In constructor initializer list, add: `budget_tracker_(budget_tracker)`

In `run()`, when creating `TurnContext`, add:
```cpp
ctx.budget_tracker = budget_tracker_;
```

Before emitting `TurnEnd` event (the `should_stop` path), query BudgetTracker:
```cpp
if (budget_tracker_) {
    auto& evt = // the TurnEnd event being prepared
    evt.turn_usage = budget_tracker_->session_usage();
    evt.turn_cost = budget_tracker_->session_cost();
}
```

This requires storing the TurnEnd event before emitting it. Since `emit(AgentEventType::TurnEnd, ctx)` creates the event internally, modify the `emit()` method to check `budget_tracker` in ctx and populate the event, or populate the event before calling `emit_event()`.

The simplest approach: after `emit(AgentEventType::TurnEnd, ctx)`, if `budget_tracker_` is set, emit a separate budget info event or modify `emit()` to check ctx.budget_tracker. The cleaner way: in the `should_stop` block, before the output line, create an AgentEvent manually with usage data and emit it.

Replace the existing `emit(AgentEventType::TurnEnd, ctx)` call in the should_stop path with:
```cpp
AgentEvent turn_end_event;
turn_end_event.type = AgentEventType::TurnEnd;
turn_end_event.iteration = ctx.iteration;
turn_end_event.agent_id = ctx.agent_id;
if (budget_tracker_) {
    turn_end_event.turn_usage = budget_tracker_->session_usage();
    turn_end_event.turn_cost = budget_tracker_->session_cost();
}
emit_event(turn_end_event);
```

- [ ] **Step 5: Modify `src/agent/CMakeLists.txt`**

Add `ea-budget` to `target_link_libraries`:
```cmake
target_link_libraries(ea-agent PUBLIC ea-core ea-common ea-platform ea-tool ea-conversation ea-budget)
```

- [ ] **Step 6: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests`
Expected: All existing tests still pass. BudgetTracker integration compiles cleanly.

- [ ] **Step 7: Commit**

```bash
git add src/agent/AgentEvent.h src/agent/TurnContext.h src/agent/AgentLoop.h src/agent/AgentLoop.cpp src/agent/CMakeLists.txt
git commit -m "feat: integrate BudgetTracker into AgentLoop with event usage data"
```

---

### Task 5: main.cpp Wiring + CLI Commands

**Files:**
- Modify: `src/main.cpp` — create BudgetTracker wrapping provider, wire into AgentLoop, add /usage and /cost commands, add usage event listener

**Interfaces:**
- Consumes: `ea::budget::BudgetTracker`, `ea::budget::SqliteUsageStore`, `ea::budget::BudgetConfig`, `ea::agent::IEventListener`, `ea::AgentEvent`

- [ ] **Step 1: Add includes to main.cpp**

After existing includes:
```cpp
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
```

- [ ] **Step 2: Create BudgetTracker and SqliteUsageStore in main.cpp**

After section 5.8 (conversation store), add section 5.9:

```cpp
// 5.9. Create budget tracker
std::unique_ptr<ea::budget::SqliteUsageStore> usage_store;
std::shared_ptr<ea::budget::BudgetTracker> budget_tracker;

if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
    std::string usage_path = cfg.budget.path;
    if (usage_path.empty()) {
        auto data_dir = ea::fs::config_dir();
        if (data_dir.ok()) {
            usage_path = data_dir.value() + "/usage.db";
        } else {
            usage_path = home + "/.embedded-agent/usage.db";
        }
    }

    usage_store = std::make_unique<ea::budget::SqliteUsageStore>(
        ea::budget::SqliteUsageStore::Config{usage_path});
    auto usage_open = usage_store->open();
    if (!usage_open.ok()) {
        EA_WARN("Usage store open failed: {}", usage_open.error().message);
        usage_store.reset();
    }

    budget_tracker = std::make_shared<ea::budget::BudgetTracker>(
        std::shared_ptr<ea::IProvider>(provider, [](ea::IProvider*){}),  // non-owning
        cfg.budget);
    if (usage_store) {
        budget_tracker->set_store(usage_store.get());
    }
}
```

Wait — the `provider` is a `unique_ptr<IProvider>`. To wrap it with BudgetTracker without losing ownership, restructure: move `provider` into a `shared_ptr` first, then pass that to BudgetTracker.

Change section 3:
```cpp
// 3. Create provider
auto provider_raw = ea::provider::create(cfg.provider);
if (!provider_raw) {
    EA_ERROR("Unknown provider type: {}", cfg.provider.type);
    std::cerr << "Unknown provider type: " << cfg.provider.type << std::endl;
    return 1;
}
auto provider = std::shared_ptr<ea::IProvider>(std::move(provider_raw));
```

Then in section 5.9:
```cpp
if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
    // ... usage_store creation as above ...

    budget_tracker = std::make_shared<ea::budget::BudgetTracker>(
        provider, cfg.budget);
    if (usage_store) {
        budget_tracker->set_store(usage_store.get());
    }
}
```

Update the `provider.get()` call in AgentLoop construction to use `budget_tracker ? budget_tracker.get() : provider.get()`.

Also update ContextCompressor creation to use the same effective provider.

- [ ] **Step 3: Add budget_tracker to AgentLoop constructor**

In the AgentLoop construction (line 228), add `budget_tracker.get()` as the 12th parameter.

- [ ] **Step 4: Add usage event listener**

After section 8.5 (event listeners), add a BudgetTracker event listener that prints usage after each LLM call:

```cpp
if (budget_tracker) {
    auto budget_listener = std::make_shared<ea::agent::FunctionalEventListener>(
        [](const ea::agent::AgentEvent& event) {
            if (event.type == ea::agent::AgentEventType::LLMResponse) {
                // Usage is in the event
                auto& u = event.usage;
                if (u.input_tokens > 0 || u.output_tokens > 0) {
                    std::cout << "\n[Usage: " << u.input_tokens << " in / "
                              << u.output_tokens << " out]" << std::flush;
                }
            }
        });
    loop.add_listener(budget_listener);
}
```

Note: This requires creating a `FunctionalEventListener` class (or using a lambda-based approach). The simplest way is to create an inline `IEventListener` implementation. Alternatively, extend the existing event system.

A simpler approach: instead of a new listener class, use the existing `LLMResponse` event that already carries `usage`. The BudgetTracker listener can be a simple struct:

```cpp
struct BudgetEventListener : public ea::agent::IEventListener {
    ea::budget::BudgetTracker* tracker_;
    explicit BudgetEventListener(ea::budget::BudgetTracker* t) : tracker_(t) {}
    void on_event(const ea::agent::AgentEvent& event) override {
        if (event.type == ea::agent::AgentEventType::LLMResponse) {
            auto su = tracker_->session_usage();
            auto sc = tracker_->session_cost();
            std::cout << "\n[Usage: " << su.input_tokens << " in / "
                      << su.output_tokens << " out | $"
                      << std::fixed << std::setprecision(4) << sc.total()
                      << " session]" << std::flush;
        }
    }
};
```

Define this as a local class in main.cpp or as a small header.

- [ ] **Step 5: Add CLI commands `/usage` and `/cost`**

In the CLI interactive loop, add before the `/history` command:

```cpp
if (input == "/usage") {
    if (budget_tracker) {
        auto su = budget_tracker->session_usage();
        auto gu = budget_tracker->global_usage();
        std::cout << "Session Usage:\n"
                  << "  Input:  " << su.input_tokens << " tokens\n"
                  << "  Output: " << su.output_tokens << " tokens\n"
                  << "  Cache:  " << su.cache_read_tokens << " read / "
                  << su.cache_write_tokens << " write\n"
                  << "  Total:  " << su.total_tokens() << " tokens\n\n"
                  << "Global Usage:\n"
                  << "  Input:  " << gu.input_tokens << " tokens\n"
                  << "  Output: " << gu.output_tokens << " tokens\n"
                  << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
    } else {
        std::cout << "Budget tracking not enabled" << std::endl;
    }
    continue;
}
if (input == "/cost") {
    if (budget_tracker) {
        auto sc = budget_tracker->session_cost();
        auto gc = budget_tracker->global_cost();
        std::cout << "Session Cost:\n"
                  << "  Total: $" << std::fixed << std::setprecision(4) << sc.total() << "\n\n"
                  << "Global Cost:\n"
                  << "  Total: $" << gc.total() << std::endl;
    } else {
        std::cout << "Budget tracking not enabled" << std::endl;
    }
    continue;
}
if (input == "/usage global") {
    if (budget_tracker) {
        auto gu = budget_tracker->global_usage();
        std::cout << "Global Usage:\n"
                  << "  Input:  " << gu.input_tokens << " tokens\n"
                  << "  Output: " << gu.output_tokens << " tokens\n"
                  << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
    }
    continue;
}
if (input == "/cost global") {
    if (budget_tracker) {
        auto gc = budget_tracker->global_cost();
        std::cout << "Global Cost:\n"
                  << "  Total: $" << std::fixed << std::setprecision(4) << gc.total() << std::endl;
    }
    continue;
}
```

- [ ] **Step 6: Set session_id on BudgetTracker when conversation starts**

When the auto-resume creates a conversation, or when a new conversation starts, set the session_id on BudgetTracker:

```cpp
if (budget_tracker && !loop.conversation_id().empty()) {
    budget_tracker->set_session_id(loop.conversation_id());
}
```

Call this after `loop.run(input)` returns in the main loop, or in the auto-resume section.

- [ ] **Step 7: Build and verify**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests`
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire BudgetTracker into CLI with /usage and /cost commands"
```

---

### Task 6: Server Mode Integration

**Files:**
- Modify: `src/server/HttpServer.h` — add `budget::BudgetTracker*` member and constructor param
- Modify: `src/server/HttpServer.cpp` — add `/api/usage`, `/api/cost`, `/api/usage/history` endpoints; embed usage in chat response
- Modify: `src/server/SessionManager.h` — add `budget::BudgetTracker*` member and constructor param
- Modify: `src/server/SessionManager.cpp` — pass BudgetTracker to AgentLoop, set session_id
- Modify: `src/server/CMakeLists.txt` — add `ea-budget` to link libraries

**Interfaces:**
- Consumes: `ea::budget::BudgetTracker`, `ea::budget::IUsageStore`

- [ ] **Step 1: Modify `src/server/SessionManager.h`**

Add include:
```cpp
#include "budget/BudgetTracker.h"
```

Add constructor param:
```cpp
SessionManager(const ServerConfig& config,
               conversation::IConversationStore* conv_store = nullptr,
               budget::BudgetTracker* budget_tracker = nullptr);
```

Add member:
```cpp
budget::BudgetTracker* budget_tracker_;
```

- [ ] **Step 2: Modify `src/server/SessionManager.cpp`**

Update constructor to store `budget_tracker_`.

In `create()`, pass `budget_tracker_` to AgentLoop as 12th param.

After creating session and before restoring conversation, set session_id on BudgetTracker:
```cpp
if (budget_tracker_) {
    budget_tracker_->set_session_id(session->id);
}
```

- [ ] **Step 3: Modify `src/server/HttpServer.h`**

Add include:
```cpp
#include "budget/BudgetTracker.h"
```

Add constructor param:
```cpp
HttpServer(ServerConfig config,
           IProvider* provider,
           tool::ToolRegistry* registry,
           security::SecurityPolicy* policy,
           IMemory* shared_memory = nullptr,
           conversation::IConversationStore* conv_store = nullptr,
           budget::BudgetTracker* budget_tracker = nullptr);
```

Add member:
```cpp
budget::BudgetTracker* budget_tracker_;
```

- [ ] **Step 4: Modify `src/server/HttpServer.cpp`**

Update constructor to accept and store `budget_tracker_`.

Update `sessions_` construction to pass `budget_tracker_`.

Embed usage in chat response (in the `POST /api/sessions/:id/chat` handler):
```cpp
if (budget_tracker_) {
    auto su = budget_tracker_->session_usage();
    auto sc = budget_tracker_->session_cost();
    resp["usage"] = {
        {"input_tokens", su.input_tokens},
        {"output_tokens", su.output_tokens},
        {"total_tokens", su.total_tokens()}
    };
    resp["cost"] = {{"total", sc.total()}};
    resp["session_usage"] = {
        {"input_tokens", su.input_tokens},
        {"output_tokens", su.output_tokens},
        {"total_tokens", su.total_tokens()}
    };
    resp["session_cost"] = {{"total", sc.total()}};
    resp["over_warn"] = budget_tracker_->is_over_warn();
    resp["over_limit"] = budget_tracker_->is_over_limit();
}
```

Add usage API endpoints (inside `if (budget_tracker_)` guard):

```cpp
// GET /api/usage
server_->Get("/api/usage", [this](const httplib::Request& req, httplib::Response& res) {
    std::string scope = req.get_param_value("scope");
    std::string session_id = req.get_param_value("session_id");
    json body;
    body["scope"] = scope.empty() ? "session" : scope;
    if (scope == "global") {
        auto u = budget_tracker_->global_usage();
        auto c = budget_tracker_->global_cost();
        body["usage"] = {{"input_tokens", u.input_tokens}, {"output_tokens", u.output_tokens},
                         {"cache_read_tokens", u.cache_read_tokens}, {"cache_write_tokens", u.cache_write_tokens},
                         {"total_tokens", u.total_tokens()}};
        body["cost"] = {{"total", c.total()}};
    } else {
        auto u = budget_tracker_->session_usage();
        auto c = budget_tracker_->session_cost();
        body["session_id"] = session_id;
        body["usage"] = {{"input_tokens", u.input_tokens}, {"output_tokens", u.output_tokens},
                         {"cache_read_tokens", u.cache_read_tokens}, {"cache_write_tokens", u.cache_write_tokens},
                         {"total_tokens", u.total_tokens()}};
        body["cost"] = {{"total", c.total()}};
    }
    body["over_warn"] = budget_tracker_->is_over_warn();
    body["over_limit"] = budget_tracker_->is_over_limit();
    set_cors_headers(&res);
    res.set_content(body.dump(), "application/json");
});

// GET /api/cost
server_->Get("/api/cost", [this](const httplib::Request& req, httplib::Response& res) {
    std::string scope = req.get_param_value("scope");
    json body;
    body["scope"] = scope.empty() ? "session" : scope;
    if (scope == "global") {
        auto c = budget_tracker_->global_cost();
        body["cost"] = {{"input_cost", c.input_cost}, {"output_cost", c.output_cost},
                        {"cache_read_cost", c.cache_read_cost}, {"cache_write_cost", c.cache_write_cost},
                        {"total", c.total()}};
    } else {
        auto c = budget_tracker_->session_cost();
        body["cost"] = {{"input_cost", c.input_cost}, {"output_cost", c.output_cost},
                        {"cache_read_cost", c.cache_read_cost}, {"cache_write_cost", c.cache_write_cost},
                        {"total", c.total()}};
    }
    body["over_warn"] = budget_tracker_->is_over_warn();
    body["over_limit"] = budget_tracker_->is_over_limit();
    set_cors_headers(&res);
    res.set_content(body.dump(), "application/json");
});

// GET /api/usage/history
server_->Get("/api/usage/history", [this](const httplib::Request& req, httplib::Response& res) {
    int limit = 50, offset = 0;
    try {
        if (!req.get_param_value("limit").empty()) limit = std::stoi(req.get_param_value("limit"));
        if (!req.get_param_value("offset").empty()) offset = std::stoi(req.get_param_value("offset"));
    } catch (...) {}
    if (limit < 1) limit = 1;
    if (limit > 1000) limit = 1000;
    if (offset < 0) offset = 0;
    // Query from usage_store if available
    json arr = json::array();
    // BudgetTracker has the store reference; for now return from in-memory
    set_cors_headers(&res);
    res.set_content(json{{"records", arr}, {"limit", limit}, {"offset", offset}}.dump(), "application/json");
});
```

- [ ] **Step 5: Modify `src/server/CMakeLists.txt`**

Add `ea-budget` to link libraries:
```cmake
target_link_libraries(ea-server PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json ea-conversation ea-budget PRIVATE httplib::httplib)
```

- [ ] **Step 6: Update main.cpp server section**

Update `HttpServer` construction to pass `budget_tracker.get()`:
```cpp
auto http_server = std::make_unique<ea::server::HttpServer>(
    srv_cfg, provider.get(), &registry, security.get(), memory.get(),
    conv_store.get(), budget_tracker.get()
);
```

Wait — the provider passed to HttpServer is used for sessions. The BudgetTracker wraps the provider. The HttpServer should receive `budget_tracker ? budget_tracker.get() : provider.get()` as the provider pointer, so that sessions use the tracked provider.

Actually, looking at the existing code, HttpServer stores `provider_` and passes it to `sessions_.create()`. The SessionManager passes it to AgentLoop. So we need the effective provider (BudgetTracker if available, else raw provider) to be the one passed to HttpServer.

- [ ] **Step 7: Build and verify**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests`
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/server/HttpServer.h src/server/HttpServer.cpp src/server/SessionManager.h src/server/SessionManager.cpp src/server/CMakeLists.txt src/main.cpp
git commit -m "feat: add Server /api/usage, /api/cost endpoints and chat usage embedding"
```

---

### Task 7: Final Verification

**Files:**
- All files from Tasks 1-6

- [ ] **Step 1: Full clean rebuild**

Run: `cd build && cmake --build . --clean-first -j$(nproc)`
Expected: Clean build, 0 errors, 0 warnings (except existing tmpnam warning).

- [ ] **Step 2: Run full test suite**

Run: `./tests/ea-tests --reporter console`
Expected: All tests pass (390+ existing + ~20 new budget tests).

- [ ] **Step 3: Run budget-specific tests**

Run: `./tests/ea-tests "[budget]"`
Expected: All budget tests pass.

- [ ] **Step 4: Verify config parsing**

Create a test TOML with `[budget]` section and `[[budget.pricing]]` entries, verify it parses correctly.

- [ ] **Step 5: Commit**

```bash
git commit --allow-empty -m "verify: Phase 4E all tests pass"
```
