# Phase 4E: Token Budget & Cost Tracking — Design Spec

**Date**: 2026-07-20
**Status**: Draft
**Depends on**: Phase 4A (OllamaProvider), Phase 4B (Server Mode), Phase 4C (Conversation Persistence)

## Problem

Token usage data flows through the system but is never accumulated, persisted, or acted upon. The `Usage` struct exists in `LLMResponse` and `StreamChunk`, and `CallProviderStep` already emits it via `AgentEvent::LLMResponse`, but the data is discarded after each event. Users cannot:

- See how many tokens a conversation has consumed
- Estimate API costs across sessions
- Set budget limits to prevent overspending
- Review historical usage patterns

## Solution

Introduce a **BudgetTracker** provider decorator that intercepts all `chat()`/`stream_chat()` calls, extracts `Usage` data, accumulates it at session and global levels, and calculates costs using a configurable model pricing table. Persist usage records to a separate SQLite file (`usage.db`) via an **IUsageStore** interface.

Key behaviors:

1. **Transparent interception**: BudgetTracker wraps any IProvider, recording usage without modifying Provider implementations
2. **Dual-level accumulation**: Session-level (reset per conversation) and global-level (cross-conversation, persisted)
3. **Cost estimation**: Configurable per-model pricing with prefix matching; unpriced models (e.g., Ollama local) default to $0
4. **Budget warnings**: Configurable warn thresholds; over-budget triggers CLI warnings and Server response flags
5. **Graceful degradation**: Persistence failures never block the conversation flow

## Architecture

### Data Model

```cpp
// src/budget/Types.h
namespace ea::budget {

struct UsageSnapshot {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;

    UsageSnapshot& operator+=(const UsageSnapshot& other);
    UsageSnapshot operator+(const UsageSnapshot& other) const;
    int total_tokens() const;
};

struct CostSnapshot {
    double input_cost = 0.0;       // USD
    double output_cost = 0.0;     // USD
    double cache_read_cost = 0.0; // USD
    double cache_write_cost = 0.0;// USD

    CostSnapshot& operator+=(const CostSnapshot& other);
    double total() const;
};

struct ModelPricing {
    std::string model_id;          // "gpt-4o", "claude-sonnet-5", etc.
    double input_per_mtok = 0.0;   // USD per 1M input tokens
    double output_per_mtok = 0.0;  // USD per 1M output tokens
    double cache_read_per_mtok = 0.0;
    double cache_write_per_mtok = 0.0;

    CostSnapshot calculate(const UsageSnapshot& usage) const;
};

struct BudgetConfig {
    std::string path;                    // usage.db path (empty = auto)
    int warn_input_tokens = 100000;
    int warn_output_tokens = 50000;
    double warn_cost_usd = 1.0;
    int max_input_tokens = 0;            // 0 = no hard limit
    int max_output_tokens = 0;
    double max_cost_usd = 0.0;
    std::vector<ModelPricing> pricing;
};

}  // namespace ea::budget
```

**UsageSnapshot** reuses the same fields as `ea::Usage` but adds arithmetic operators and `total_tokens()`. Conversion from `ea::Usage` to `UsageSnapshot` is a simple field copy.

**ModelPricing::calculate()** multiplies each token count by the corresponding per-Mtok rate, divided by 1,000,000:

```
input_cost = input_tokens * input_per_mtok / 1_000_000
```

**Model matching**: prefix match on `model_id`. The model string from the Provider response (or the configured default model) is matched against pricing entries by checking if the model string starts with `model_id`. First match wins. Unmatched models default to $0 cost (e.g., Ollama local models).

### BudgetTracker Decorator

```cpp
// src/budget/BudgetTracker.h
namespace ea::budget {

class BudgetTracker : public IProvider {
public:
    BudgetTracker(std::shared_ptr<IProvider> inner, BudgetConfig config);

    // IProvider interface — intercept and record usage
    std::string name() const override;
    std::vector<std::string> list_models() const override;
    ProviderCapabilities capabilities() const override;
    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) override;
    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) override;

    // Query interface
    UsageSnapshot session_usage() const;
    CostSnapshot session_cost() const;
    UsageSnapshot global_usage() const;
    CostSnapshot global_cost() const;
    void reset_session();
    void reset_global();

    // Budget check
    bool is_over_warn() const;
    bool is_over_limit() const;

    // Persistence
    void set_store(IUsageStore* store);
    void set_session_id(const std::string& session_id);
    void flush();  // Write pending records to store

private:
    void record_usage(const Usage& usage, const std::string& model);
    const ModelPricing* find_pricing(const std::string& model) const;
    CostSnapshot calculate_cost(const Usage& usage, const std::string& model) const;
    void check_budget() const;

    std::shared_ptr<IProvider> inner_;
    BudgetConfig config_;
    IUsageStore* store_ = nullptr;
    std::string session_id_;
    mutable std::mutex mutex_;
    UsageSnapshot session_usage_;
    CostSnapshot session_cost_;
    UsageSnapshot global_usage_;
    CostSnapshot global_cost_;
    std::string last_model_;
};

}  // namespace ea::budget
```

**chat() interception**:

```
1. Call inner_->chat()
2. If result.ok(), extract result.value().usage
3. record_usage(usage, model)
4. Return result (unchanged)
```

**stream_chat() interception**:

```
1. Wrap on_chunk callback:
   - Forward all chunks to original on_chunk
   - If chunk.type == Done && chunk.usage has value:
       record_usage(chunk.usage.value(), model)
   - If chunk.type == Content && chunk.usage has value (OpenAI stream_options):
       Accumulate usage from final content chunk
2. Call inner_->stream_chat() with wrapped callback
3. Return result
```

**record_usage()**:

```
1. Lock mutex_
2. Convert Usage → UsageSnapshot, add to session_usage_ and global_usage_
3. Calculate cost via find_pricing(model) → calculate()
4. Add to session_cost_ and global_cost_
5. If store_ is set, create UsageRecord and write to store
6. check_budget()
7. Unlock
```

**Provider chain** (assembled in main.cpp / SessionManager):

```
AgentLoop → BudgetTracker → ReliableProvider → OpenAIProvider
```

BudgetTracker wraps the entire provider chain below it, so it sees all LLM calls including those from ContextCompressor and ProgressiveMemoryStrategy (which call `provider->chat()` directly through the AgentLoop's provider pointer).

### IUsageStore Interface

```cpp
// src/budget/IUsageStore.h
namespace ea::budget {

struct UsageRecord {
    std::string id;              // "usage_" + 8 hex chars
    std::string session_id;      // Associated session/conversation
    std::string model;           // Model used
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;
    double cost_usd = 0.0;
    std::string timestamp;       // ISO 8601
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

### Database Schema

Stored in a separate SQLite file (`usage.db`), independent of `memory.db` and `conversations.db`:

```sql
CREATE TABLE usage_records (
    id          TEXT PRIMARY KEY,
    session_id  TEXT NOT NULL DEFAULT '',
    model       TEXT NOT NULL DEFAULT '',
    input_tokens   INTEGER NOT NULL DEFAULT 0,
    output_tokens  INTEGER NOT NULL DEFAULT 0,
    cache_read_tokens  INTEGER DEFAULT 0,
    cache_write_tokens INTEGER DEFAULT 0,
    cost_usd    REAL NOT NULL DEFAULT 0.0,
    timestamp   TEXT NOT NULL
);

CREATE INDEX idx_usage_session ON usage_records(session_id);
CREATE INDEX idx_usage_timestamp ON usage_records(timestamp DESC);
```

**SqliteUsageStore** follows the same patterns as `SqliteConversationStore`:
- WAL mode for concurrent reads
- Thread-safe ID generation (mutex-protected mt19937)
- Thread-safe timestamp generation (gmtime_r + millisecond precision)
- `total_usage()` and `total_cost()` use SQL SUM aggregation for efficiency

### AgentLoop Integration

AgentLoop requires minimal changes — BudgetTracker is a Provider decorator, so AgentLoop sees it as a regular IProvider. One small addition to `AgentEvent`:

```cpp
// AgentEvent.h addition
struct AgentEvent {
    // ... existing fields ...
    UsageSnapshot turn_usage;     // TurnEnd: cumulative usage for this turn
    CostSnapshot turn_cost;       // TurnEnd: cumulative cost for this turn
};
```

In `AgentLoop::run()`, after `should_stop` and before emitting `TurnEnd`, query the BudgetTracker (via `dynamic_cast` or stored pointer) for session usage/cost and populate the event fields.

**BudgetTracker access in AgentLoop**: Add an optional `BudgetTracker*` to `TurnContext`:

```cpp
// TurnContext.h addition
struct TurnContext {
    // ... existing fields ...
    budget::BudgetTracker* budget_tracker = nullptr;
};
```

AgentLoop sets this when creating TurnContext if its provider is a BudgetTracker. This avoids dynamic_cast in the hot path.

**session_id mapping**: The `session_id` in `UsageRecord` maps to `AgentLoop::conversation_id_` in CLI mode, and to the Session ID in Server mode. This is passed through `TurnContext` when available, or set explicitly via `BudgetTracker::set_session_id()`.

### CLI Mode

**Real-time display**: Register an `IEventListener` that listens for `LLMResponse` events and prints a usage summary line after each LLM call:

```
[Usage: 1,234 in / 567 out | $0.008 | Session: 12,345 in / 5,678 out | $0.08]
```

**CLI commands** (added to the interactive loop in main.cpp):

| Command | Action |
|---------|--------|
| `/usage` | Show current session token usage |
| `/cost` | Show current session cost breakdown by model |
| `/usage global` | Show global cumulative token usage |
| `/cost global` | Show global cumulative cost |

**`/usage` output**:
```
Session Usage:
  Input:  12,345 tokens
  Output: 5,678 tokens
  Cache:  2,000 read / 500 write
  Total:  20,523 tokens

Global Usage:
  Input:  156,789 tokens
  Output: 89,012 tokens
  Total:  245,801 tokens
```

**`/cost` output**:
```
Session Cost:
  gpt-4o:  $0.08 (12,345 in / 5,678 out)
  Total:   $0.08

Global Cost:
  gpt-4o:  $1.23
  llama3:  $0.00 (local)
  Total:   $1.23
```

### Server Mode

**New HTTP API endpoints**:

```
GET /api/usage?scope=session&session_id=xxx  — Query session usage
GET /api/usage?scope=global                  — Query global usage
GET /api/cost?scope=session&session_id=xxx   — Query session cost
GET /api/cost?scope=global                   — Query global cost
GET /api/usage/history?limit=50&offset=0     — Usage history records
```

**GET /api/usage response**:
```json
{
  "scope": "session",
  "session_id": "s1",
  "usage": {
    "input_tokens": 12345,
    "output_tokens": 5678,
    "cache_read_tokens": 2000,
    "cache_write_tokens": 500,
    "total_tokens": 20523
  },
  "over_warn": false,
  "over_limit": false
}
```

**GET /api/cost response**:
```json
{
  "scope": "session",
  "session_id": "s1",
  "cost": {
    "input_cost": 0.031,
    "output_cost": 0.057,
    "cache_read_cost": 0.005,
    "cache_write_cost": 0.001,
    "total": 0.094
  },
  "over_warn": false,
  "over_limit": false
}
```

**Chat response embedding**: `POST /api/sessions/:id/chat` response gains `usage` and `cost` fields:

```json
{
  "response": "The capital of France is Paris.",
  "usage": {
    "input_tokens": 234,
    "output_tokens": 12,
    "total_tokens": 246
  },
  "cost": {
    "total": 0.002
  },
  "session_usage": {
    "input_tokens": 12345,
    "output_tokens": 5678,
    "total_tokens": 20523
  },
  "session_cost": {
    "total": 0.088
  }
}
```

### Budget Warnings

When `warn_*` thresholds are exceeded:

- **CLI**: Yellow warning line: `[⚠ Budget Warning: $1.05 exceeds1.00 limit]`
- **Server**: Chat response includes `over_warn: true`; log WARN message
- **Event**: New `AgentEventType::BudgetWarning` event in AgentEvent

When `max_*` thresholds are exceeded (hard limit, if configured):

- **CLI**: Red warning line: `[⛔ Budget Limit Reached: $2.05 exceeds $2.00 limit]`
- **Server**: Chat response includes `over_limit: true`; log ERROR message
- **Behavior**: Warning only — does not forcibly stop the conversation (per user's choice of "warn mode")

### Configuration

The `budget::BudgetConfig` struct (defined in `src/budget/Types.h`) serves as both the runtime config and the TOML parse target. `src/config/Config.h` includes `budget/Types.h` and uses `budget::BudgetConfig` directly in `AppConfig` — avoiding a duplicate config struct.

```cpp
// src/config/Config.h addition
#include "budget/Types.h"

struct AppConfig {
    // ... existing fields ...
    budget::BudgetConfig budget;   // after conversation
};
```

TOML:

```toml
[budget]
path = ""
warn_input_tokens = 100000
warn_output_tokens = 50000
warn_cost_usd = 1.0
max_input_tokens = 0
max_output_tokens = 0
max_cost_usd = 0.0

[[budget.pricing]]
model_id = "gpt-4o"
input_per_mtok = 2.50
output_per_mtok = 10.00
cache_read_per_mtok = 1.25
cache_write_per_mtok = 2.50

[[budget.pricing]]
model_id = "gpt-4o-mini"
input_per_mtok = 0.15
output_per_mtok = 0.60

[[budget.pricing]]
model_id = "claude-sonnet-5"
input_per_mtok = 3.00
output_per_mtok = 15.00
cache_read_per_mtok = 0.30
cache_write_per_mtok = 3.75

[[budget.pricing]]
model_id = "claude-haiku-4-5-20251001"
input_per_mtok = 0.80
output_per_mtok = 4.00
cache_read_per_mtok = 0.08
cache_write_per_mtok = 1.00

[[budget.pricing]]
model_id = "llama3"
input_per_mtok = 0.0
output_per_mtok = 0.0
```

### Namespace

`ea::budget` — independent concern, parallel to agent/memory/provider/conversation.

### File Structure

```
src/budget/
  Types.h                     — UsageSnapshot, CostSnapshot, ModelPricing, BudgetConfig
  IUsageStore.h               — Interface + UsageRecord
  SqliteUsageStore.h          — Class declaration
  SqliteUsageStore.cpp        — Implementation
  BudgetTracker.h             — Decorator declaration
  BudgetTracker.cpp           — Implementation
  CMakeLists.txt              — ea-budget OBJECT library

src/config/
  Config.h / .cpp             — BudgetConfig addition

src/agent/
  AgentEvent.h                — turn_usage, turn_cost fields
  TurnContext.h               — budget_tracker pointer
  AgentLoop.cpp               — Populate TurnContext.budget_tracker

src/server/
  HttpServer.cpp              — /api/usage, /api/cost, /api/usage/history endpoints
  SessionManager.h / .cpp     — Pass BudgetTracker to AgentLoop

src/main.cpp                  — Create BudgetTracker, wire into provider chain, CLI commands, event listener

tests/
  test_budget_tracker.cpp     — BudgetTracker unit tests
  test_usage_store.cpp        — SqliteUsageStore unit tests

CMakeLists.txt                — add_subdirectory(src/budget), link ea-budget
tests/CMakeLists.txt          — Add test files, link ea-budget
```

### Dependencies

```
ea-budget → ea-core (Types.h, Result.h, Error.h, IProvider.h)
          → nlohmann/json
          → sqlite3

ea-agent → ea-budget (optional, via BudgetTracker* pointer)
ea-server → ea-budget (optional)
```

### Error Handling

| Scenario | Handling |
|----------|----------|
| BudgetTracker::record_usage() store write fails | Log warning, in-memory counters still accurate |
| SqliteUsageStore::open() fails | Log warning, BudgetTracker operates without persistence |
| Model not in pricing table | Cost defaults to $0 |
| Usage fields are 0 (provider didn't report) | No-op, skip recording |
| BudgetTracker wraps nullptr inner | Error on construction |
| /api/usage query with invalid scope | Return 400 |
| /api/usage/history with invalid limit/offset | Clamp: limit 1-1000, offset ≥ 0 |

**Core principle**: Budget tracking failures never block the conversation flow.

### Test Plan

| Type | Coverage | Tags |
|------|----------|------|
| Unit | UsageSnapshot arithmetic (+=, +, total_tokens) | `[budget]` |
| Unit | CostSnapshot arithmetic (+=, total) | `[budget]` |
| Unit | ModelPricing::calculate() correctness | `[budget]` |
| Unit | Model prefix matching (exact, prefix, no match) | `[budget]` |
| Unit | BudgetTracker chat() interception and recording | `[budget]` |
| Unit | BudgetTracker stream_chat() usage extraction | `[budget]` |
| Unit | BudgetTracker session/global dual accumulation | `[budget]` |
| Unit | BudgetTracker reset_session() / reset_global() | `[budget]` |
| Unit | BudgetTracker budget warn/limit detection | `[budget]` |
| Unit | BudgetTracker with no store (in-memory only) | `[budget]` |
| Unit | SqliteUsageStore CRUD (record, query, clear) | `[budget]` |
| Unit | SqliteUsageStore total_usage/total_cost aggregation | `[budget]` |
| Unit | SqliteUsageStore query by session_id | `[budget]` |
| Unit | SqliteUsageStore query with limit/offset | `[budget]` |
| Integration | AgentLoop + BudgetTracker end-to-end | `[agent]` `[budget]` |
| Integration | Server /api/usage endpoint | `[server]` `[budget]` |
| Integration | Server /api/cost endpoint | `[server]` `[budget]` |
| Integration | Chat response usage embedding | `[server]` `[budget]` |

### Out of Scope

- Multi-user quotas / rate limiting (requires user system)
- Per-minute / per-second token rate limits
- Date-range usage queries (can be added later)
- Usage export to CSV/JSON reports
- Budget auto-recharge / renewal mechanisms
- Token counting without provider-reported usage (estimation from text length)
