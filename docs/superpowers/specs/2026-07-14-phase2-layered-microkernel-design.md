# Phase 2 Design: Layered Microkernel Architecture

**Date:** 2026-07-14
**Status:** Approved
**Scope:** Agent Loop, Provider, Tool System, Memory, Build Config

## Overview

Transform embedded-agent from a Phase 1 MVP into a **general-purpose agent framework** with compile-time mode switching (CLI / Embedded / Server). The architecture follows a **layered microkernel** pattern: core interfaces + pluggable strategies/decorators, inspired by zeroclaw's trait-driven design and hermes' lifecycle patterns.

**Implementation strategy:** Parallel MVP — each subsystem ships a minimal viable version, integrated incrementally.

---

## 1. Agent Loop Reinforcement

### Current State

Single `run()` while-loop with `max_iterations=90` as the only guard. No loop detection, no history pruning, no context compression, no structured streaming tool events.

### TurnStep Chain

Decompose the monolithic loop into a pluggable `ITurnStep` sequence:

```cpp
struct TurnContext {
    std::vector<Message>& messages;
    std::vector<ToolCall> pending_tool_calls;
    std::vector<ToolResult> tool_results;
    int iteration = 0;
    int budget_remaining;
    bool should_stop = false;
};

class ITurnStep {
public:
    virtual ~ITurnStep() = default;
    virtual Result<void> execute(TurnContext& ctx) = 0;
    virtual std::string name() const = 0;
};
```

**Built-in steps (execution order):**

| Step | Responsibility |
|------|---------------|
| `HistoryPruneStep` | Trim messages exceeding token budget, leave breadcrumb markers |
| `BuildToolSpecsStep` | Build tool descriptions from active Toolsets |
| `CallProviderStep` | Call LLM (streaming or non-streaming) |
| `ParseResponseStep` | Parse LLM response, extract tool_calls |
| `LoopDetectStep` | Three-pattern detection, escalating intervention |
| `ExecuteToolsStep` | Parallel/sequential tool execution, output truncation |
| `CollectResultsStep` | Collect tool results, append to message history |

### Loop Detector

Three detection patterns (from zeroclaw):

1. **Exact repeat** — same tool+args 3+ times consecutively
2. **Ping-pong** — two tools alternating for 4+ cycles
3. **No progress** — same tool called 5+ times with identical result hash

Escalation: `Continue → Warn → Block → Break`

```cpp
enum class LoopAction { Continue, Warn, Block, Break };

class LoopDetector {
public:
    LoopAction check(const ToolCall& call, const ToolResult& result);
    void reset();
private:
    std::deque<ToolCall> recent_calls_;
    int identical_count_ = 0;
    int ping_pong_count_ = 0;
};
```

### Context Compression

When token budget is insufficient, compress middle turns while protecting head and tail. Phase 2 implements simple token counting + pruning; full LLM-based summarization is a pluggable step for later.

### Interrupt Mechanism

Replace current `interrupt()` with `std::atomic<bool>` flag, checked before each step execution.

### New Files (estimated)

- `src/agent/TurnContext.h`
- `src/agent/ITurnStep.h`
- `src/agent/LoopDetector.h/.cpp`
- `src/agent/steps/HistoryPruneStep.h/.cpp`
- `src/agent/steps/BuildToolSpecsStep.h/.cpp`
- `src/agent/steps/CallProviderStep.h/.cpp`
- `src/agent/steps/ParseResponseStep.h/.cpp`
- `src/agent/steps/LoopDetectStep.h/.cpp`
- `src/agent/steps/ExecuteToolsStep.h/.cpp`
- `src/agent/steps/CollectResultsStep.h/.cpp`

---

## 2. Provider Enhancement

### Current State

`IProvider` has only `chat()` and `stream_chat()`. Three implementations (OpenAI, Anthropic, Ollama stub). No capability declaration, no failover, no model routing. Provider differences handled by hardcoded if/else.

### ProviderCapabilities

```cpp
struct ProviderCapabilities {
    bool native_tool_calling = true;
    bool streaming = true;
    bool vision = false;
    bool prompt_caching = false;
    bool extended_thinking = false;
};

class IProvider {
    // Existing interface (unchanged)
    virtual Result<LLMResponse> chat(...) = 0;
    virtual Result<void> stream_chat(...) = 0;

    // New
    virtual ProviderCapabilities capabilities() const = 0;
};
```

Each provider declares its capabilities. Agent Loop and ToolSpec construction adapt based on capabilities, eliminating provider-name if/else.

### ReliableProvider (Retry + Fallback Decorator)

```cpp
class ReliableProvider : public IProvider {
public:
    struct Config {
        int max_retries = 3;
        std::chrono::milliseconds base_delay{1000};
        std::vector<std::shared_ptr<IProvider>> fallbacks;
    };

    Result<LLMResponse> chat(...) override;
    // 1. Call primary provider
    // 2. On retryable error (429/5xx), exponential backoff retry
    // 3. On non-retryable or retries exhausted, try fallback[0], fallback[1]...
};
```

**Error classification:**

```cpp
enum class ErrorClass { Retryable, NonRetryable, RateLimit };

ErrorClass classify_error(const Error& err) {
    if (err.code == ErrorCode::RateLimit) return ErrorClass::RateLimit;
    if (err.code == ErrorCode::NetworkError && err.http_status >= 500) return ErrorClass::Retryable;
    if (err.code == ErrorCode::Timeout) return ErrorClass::Retryable;
    return ErrorClass::NonRetryable;
}
```

### RouterProvider (Model Routing Decorator)

Add `route_hint` field to `ChatOptions` (in `Types.h`):

```cpp
struct ChatOptions {
    float temperature = 0.7f;
    int max_tokens = 4096;
    std::optional<std::string> stop;
    int top_p = 1;
    bool stream = false;
    std::optional<std::string> route_hint;  // NEW: "code", "fast", "vision", "cheap"
};
```

```cpp
struct RouteRule {
    std::string model_hint;     // "code", "fast", "vision", "cheap"
    std::string provider_type;
    std::string model;
};

class RouterProvider : public IProvider {
    // Select provider+model based on ChatOptions route_hint
    // Default route when no hint provided
};
```

### PromptGuidedTools (Fallback for Non-Tool-Calling Providers)

```cpp
class PromptGuidedTools {
public:
    static std::string inject_tools_into_prompt(
        const std::string& system_prompt,
        const std::vector<ToolSpec>& tools);
};
```

For providers without native tool calling (certain local models), inject tool descriptions as text into the system prompt.

### CredentialPool (Simplified)

```cpp
struct CredentialSlot {
    std::string api_key;
    std::string model;
    bool healthy = true;
    int consecutive_errors = 0;
};

class CredentialPool {
public:
    Result<CredentialSlot> acquire();
    void release(const CredentialSlot& slot, bool success);
private:
    std::vector<CredentialSlot> slots_;
    size_t current_index_ = 0;
};
```

Multiple credentials per provider, automatic rotation, consecutive-failure health tracking.

### New Files (estimated)

- `src/provider/ProviderCapabilities.h`
- `src/provider/ReliableProvider.h/.cpp`
- `src/provider/RouterProvider.h/.cpp`
- `src/provider/CredentialPool.h/.cpp`
- `src/provider/PromptGuidedTools.h/.cpp`
- `src/provider/ErrorClassifier.h/.cpp`

---

## 3. Tool System Extension

### Current State

`ToolRegistry` flat-registers all tools. All tools always exposed to LLM. No conditional availability, no grouping, no per-tool output limits.

### Toolset (Grouped Tools with Conditional Availability)

```cpp
class Toolset {
public:
    explicit Toolset(std::string name);

    void add(std::unique_ptr<ITool> tool);
    void add(std::unique_ptr<ITool> tool, CheckFn check);

    std::vector<ToolSpec> active_specs() const;  // Only check-passing tools
    Result<ToolResult> execute(const std::string& tool_name, const json& args);

private:
    using CheckFn = std::function<bool()>;
    struct Entry {
        std::unique_ptr<ITool> tool;
        CheckFn check;  // nullptr = always available
    };
    std::string name_;
    std::vector<Entry> entries_;
};
```

### ToolRegistry Refactor

```cpp
class ToolRegistry {
public:
    void register_toolset(std::unique_ptr<Toolset> set);
    void activate(const std::string& set_name);
    void deactivate(const std::string& set_name);

    std::vector<ToolSpec> active_specs() const;
    Result<ToolResult> execute(const std::string& tool_name, const json& args);

private:
    std::map<std::string, std::unique_ptr<Toolset>> toolsets_;
    std::set<std::string> active_sets_;
};
```

### Built-in Toolsets

| Toolset | Tools | Notes |
|---------|-------|-------|
| `core` | shell, memory | Always active, base capability |
| `file` | file (read/write/edit) | File ops, workspace-constrained in Supervised mode |
| `search` | search_files | Code/file search, available in ReadOnly |
| `web` | web (search/fetch) | Network access, requires connectivity |
| `system` | platform_info | System info, read-only |

### Conditional Availability Examples

```cpp
// Web toolset only when network is available
web_set->add(std::make_unique<WebTool>(&client), []() {
    return net::is_available();
});

// Shell restricted on Android
core_set->add(std::make_unique<ShellTool>(), []() {
    return !platform::is_android() || security::has_shell_access();
});
```

### Tool Output Control

```cpp
struct ToolOutputConfig {
    size_t max_bytes = 65536;
    std::map<std::string, size_t> per_tool;
    std::string truncate_marker = "\n...[truncated]";
};

std::string truncate_output(const std::string& output, size_t max_bytes,
                            const std::string& marker);
```

### ITool Interface Extension

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual Result<ToolResult> execute(const json& args) = 0;

    // New: tool metadata
    virtual bool is_mutating() const { return true; }
    virtual bool is_dangerous() const { return false; }
};
```

- `is_mutating()` — SecurityPolicy uses this (ReadOnly mode only allows non-mutating tools)
- `is_dangerous()` — Approval flow (Phase 3)

### New Files (estimated)

- `src/tool/Toolset.h/.cpp`
- `src/tool/ToolOutputConfig.h/.cpp`
- Refactor `src/tool/ToolRegistry.h/.cpp`

---

## 4. Memory System Upgrade

### Current State

`IMemory` has CRUD only (store/recall/forget/list/count). `SqliteMemory` is the sole backend. No lifecycle hooks, no prefetch, no agent scoping, no background sync.

### IMemory Lifecycle Extension

```cpp
class IMemory {
public:
    virtual ~IMemory() = default;

    // Existing CRUD (unchanged)
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;

    // New: lifecycle hooks
    virtual Result<void> open() { return {}; }
    virtual Result<void> close() { return {}; }
    virtual std::string system_prompt_block() const { return {}; }

    // New: turn-level hooks
    virtual void on_turn_start(const std::string& user_input) { (void)user_input; }
    virtual void on_turn_end(const std::string& assistant_output) { (void)assistant_output; }
    virtual void on_pre_compress() {}
};
```

### MemoryManager (Orchestration Layer)

```cpp
class MemoryManager {
public:
    explicit MemoryManager(std::unique_ptr<IMemory> backend);

    // Turn lifecycle
    std::vector<MemoryEntry> prefetch(const std::string& user_input);
    void sync_turn(const std::string& user_input,
                   const std::string& assistant_output);

    // Delegate to underlying IMemory
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5);
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10);

    // System prompt injection
    std::string build_memory_block() const;

private:
    std::unique_ptr<IMemory> backend_;
    std::vector<MemoryEntry> cached_context_;
    // Background sync queue (Phase 2: synchronous; Phase 3: thread pool)
};
```

### Agent-Scoped Memory

```cpp
struct MemoryScope {
    std::string agent_id;
    std::string session_id;
    std::set<std::string> read_allowlist;
};

class ScopedMemory : public IMemory {
public:
    ScopedMemory(std::unique_ptr<IMemory> backend, MemoryScope scope);

    // store auto-attaches agent_id
    Result<std::string> store(...) override;

    // recall defaults to own agent data; read_allowlist enables cross-agent reads
    Result<std::vector<MemoryEntry>> recall(...) override;

private:
    std::unique_ptr<IMemory> backend_;
    MemoryScope scope_;
};
```

### MemoryBackend Factory

```cpp
enum class MemoryBackendType { Sqlite, InMemory, Null };

class MemoryFactory {
public:
    static std::unique_ptr<IMemory> create(MemoryBackendType type,
                                            const json& config = {});
};
```

| Backend | Use Case | Config |
|---------|----------|--------|
| `Sqlite` | Production default | `path`, `enable_fts5`, `enable_wal` |
| `InMemory` | Testing / ephemeral | None |
| `Null` | Memory disabled | None |

### SqliteMemory Enhancements

- `system_prompt_block()` — Formatted summary of recent high-importance memories
- `on_turn_end()` — Auto-extract key info from turn and store (when `auto_memory=true`)
- `on_pre_compress()` — Before context compression, store summary of about-to-be-pruned conversation

### Agent Loop Integration

```
TurnContext gains MemoryManager* field

BuildToolSpecsStep:
  - Call memory->prefetch(user_input)
  - Inject prefetch results into TurnContext

CollectResultsStep:
  - Call memory->sync_turn(user_input, assistant_output)

HistoryPruneStep:
  - Before compression, call memory->on_pre_compress()
```

### New Files (estimated)

- `src/memory/MemoryManager.h/.cpp`
- `src/memory/ScopedMemory.h/.cpp`
- `src/memory/MemoryFactory.h/.cpp`
- Enhance `src/memory/SqliteMemory.h/.cpp`

---

## 5. Build Configuration & Conditional Trimming

### CMake Options

```cmake
# Run mode
option(EA_MODE_CLI       "Build CLI agent mode"       ON)
option(EA_MODE_EMBEDDED  "Build embedded device mode" OFF)
option(EA_MODE_SERVER    "Build server runtime mode"  OFF)

# Subsystem toggles
option(EA_ENABLE_MEMORY     "Enable persistent memory"     ON)
option(EA_ENABLE_STREAMING  "Enable SSE streaming"         ON)
option(EA_ENABLE_TOOLS_WEB  "Enable web tool (requires TLS)" ON)
option(EA_ENABLE_TOOLS_SHELL "Enable shell tool"           ON)
option(EA_ENABLE_ROUTER     "Enable model router provider" OFF)
option(EA_ENABLE_FALLBACK   "Enable provider fallback"     OFF)

# Embedded optimization
option(EA_MINIMAL_BUILD     "Strip all non-essential features" OFF)
```

### Mode Presets

| Option | CLI | Embedded | Server |
|--------|-----|----------|--------|
| `EA_MODE_CLI` | ON | OFF | OFF |
| `EA_MODE_EMBEDDED` | OFF | ON | OFF |
| `EA_MODE_SERVER` | OFF | OFF | ON |
| `EA_ENABLE_MEMORY` | ON | ON | ON |
| `EA_ENABLE_STREAMING` | ON | OFF | ON |
| `EA_ENABLE_TOOLS_WEB` | ON | OFF | ON |
| `EA_ENABLE_TOOLS_SHELL` | ON | Conditional | ON |
| `EA_ENABLE_ROUTER` | OFF | OFF | ON |
| `EA_ENABLE_FALLBACK` | OFF | OFF | ON |
| `EA_MINIMAL_BUILD` | OFF | ON | OFF |

### Conditional Compilation

Generated header `include/ea/build_config.h` via CMake `configure_file`:

```cpp
#pragma once

#cmakedefine EA_MODE_CLI
#cmakedefine EA_MODE_EMBEDDED
#cmakedefine EA_MODE_SERVER
#cmakedefine EA_ENABLE_MEMORY
#cmakedefine EA_ENABLE_STREAMING
#cmakedefine EA_ENABLE_TOOLS_WEB
#cmakedefine EA_ENABLE_TOOLS_SHELL
#cmakedefine EA_ENABLE_ROUTER
#cmakedefine EA_ENABLE_FALLBACK
```

Usage:

```cpp
#ifdef EA_ENABLE_MEMORY
    auto memory = MemoryFactory::create(backend_type, cfg);
    memory_mgr = std::make_unique<MemoryManager>(std::move(memory));
#endif
```

### Toolset Auto-Trimming

```cpp
void register_default_toolsets(ToolRegistry& registry) {
    auto core = std::make_unique<Toolset>("core");
#ifdef EA_ENABLE_TOOLS_SHELL
    core->add(std::make_unique<ShellTool>());
#endif
    core->add(std::make_unique<MemoryTool>(memory_mgr.get()));
    registry.register_toolset(std::move(core));

#ifdef EA_ENABLE_TOOLS_WEB
    auto web = std::make_unique<Toolset>("web");
    web->add(std::make_unique<WebTool>(&client));
    registry.register_toolset(std::move(web));
#endif
}
```

### Link Trimming

Embedded mode: skip mbedtls (no TLS = no web tool), skip spdlog file_sink (no log files), significantly reducing binary size.

---

## Scope Summary

| Subsystem | Core Change | New Files (est.) |
|-----------|-------------|-----------------|
| Agent Loop | TurnStep chain + LoopDetector + HistoryPrune + atomic interrupt | 6-8 .h/.cpp pairs |
| Provider | ProviderCapabilities + ReliableProvider + RouterProvider + CredentialPool + PromptGuidedTools | 5-6 .h/.cpp pairs |
| Tool | Toolset + conditional availability + output truncation + ITool metadata | 3-4 .h/.cpp pairs |
| Memory | MemoryManager + ScopedMemory + MemoryFactory + IMemory lifecycle | 4-5 .h/.cpp pairs |
| Build | build_config.h + CMake mode presets + conditional trimming | 2-3 files |

**Total estimated new files:** ~25-30 source files

**Deferred to Phase 3:** MCP client, approval system, subagent delegation, WASM plugins, background thread pool for memory sync, LLM-based context compression.
