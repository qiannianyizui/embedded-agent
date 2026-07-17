# Phase 3F: Event/Hook System — Design Spec

**Date:** 2026-07-18
**Status:** Draft
**Depends on:** Phase 2D (Agent Loop), Phase 3E (Streaming Integration)

## Overview

Add an event system to AgentLoop that emits lifecycle events at key points (turn start/end, tool calls, LLM responses, errors, interrupts). Consumers implement `IEventListener` and register via `add_listener()`. Events are dispatched synchronously. This provides a foundation for observability (logging, metrics, audit) and real-time notification (Server mode WebSocket push).

## Goals

- Core 7 event types covering the agent lifecycle
- `IEventListener` interface with `on_event()` method
- AgentLoop holds a listener list, emits events synchronously
- Zero overhead when no listeners are registered
- Built-in `LoggingEventListener` for debug mode
- Synchronous dispatch — listeners must not block (documented contract)
- Compatible with Subagent (agent_id field for source identification)

## Event Types

### AgentEvent Structure

Defined in `src/agent/AgentEvent.h`:

```cpp
#pragma once
#include "core/Types.h"
#include <string>

namespace ea::agent {

enum class AgentEventType {
    TurnStart,       // Iteration begins
    TurnEnd,         // Iteration ends (should_stop)
    ToolCallStart,   // Tool execution begins
    ToolCallEnd,     // Tool execution completes (with result)
    LLMResponse,     // LLM returns a response
    Error,           // An error occurred
    Interrupt        // Agent was interrupted
};

struct AgentEvent {
    AgentEventType type;
    int iteration = 0;
    std::string agent_id;           // Empty for main agent, set for subagents

    // Event data — different fields used per event type
    std::string user_input;         // TurnStart
    std::string assistant_output;   // TurnEnd, LLMResponse
    std::string tool_name;          // ToolCallStart, ToolCallEnd
    json tool_arguments;            // ToolCallStart
    std::string tool_result;        // ToolCallEnd
    bool tool_error = false;        // ToolCallEnd
    std::string error_message;      // Error
    Usage usage;                    // LLMResponse (token usage)
};

}  // namespace ea::agent
```

### Design Rationale

- **Single struct for all events:** distinguished by `type`, different fields used per type. Avoids 7 independent structs + variant complexity.
- **Value-type fields:** strings, json, bool, int — cheap to copy.
- **agent_id预留:** empty for main agent, set for subagents — enables source identification.

## IEventListener Interface

Defined in `src/agent/IEventListener.h`:

```cpp
#pragma once
#include "AgentEvent.h"

namespace ea::agent {

class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void on_event(const AgentEvent& event) = 0;
};

}  // namespace ea::agent
```

Single method — listeners dispatch internally by `event.type`.

## AgentLoop Integration

### New Members and Methods

```cpp
class AgentLoop {
public:
    // ... existing interface ...

    // Event listener management
    void add_listener(std::shared_ptr<IEventListener> listener);
    void remove_listener(const std::shared_ptr<IEventListener>& listener);

private:
    void emit(AgentEventType type, const TurnContext& ctx);

    std::vector<std::shared_ptr<IEventListener>> listeners_;
};
```

### emit() Helper

`emit()` constructs an `AgentEvent` from the `TurnContext` and synchronously calls `on_event()` on each registered listener:

```cpp
void AgentLoop::emit(AgentEventType type, const TurnContext& ctx) {
    if (listeners_.empty()) return;

    AgentEvent event;
    event.type = type;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;

    // Fill type-specific fields
    switch (type) {
    case AgentEventType::TurnStart:
        // user_input from last user message in history
        for (auto it = ctx.messages.rbegin(); it != ctx.messages.rend(); ++it) {
            if (it->role == Role::User) {
                event.user_input = it->content;
                break;
            }
        }
        break;
    case AgentEventType::TurnEnd:
        event.assistant_output = ctx.response.content;
        break;
    case AgentEventType::LLMResponse:
        event.assistant_output = ctx.response.content;
        event.usage = ctx.response.usage;
        break;
    case AgentEventType::Error:
        // Error events use emit_event() directly with error_message filled
        break;
    default:
        break;
    }

    for (const auto& listener : listeners_) {
        listener->on_event(event);
    }
}
```

For `ToolCallStart`/`ToolCallEnd` and `Error` events with specific data that `emit()` cannot infer from `TurnContext` alone, the step code constructs the `AgentEvent` directly and calls listeners:

```cpp
// Helper for direct event emission with custom data
void AgentLoop::emit_event(const AgentEvent& event) {
    for (const auto& listener : listeners_) {
        listener->on_event(event);
    }
}
```

This is a second private helper for steps that need to emit events with step-specific data. Steps access it through `TurnContext` (see below).

## Event Emission Points

| Event | Location | Trigger |
|-------|----------|---------|
| `TurnStart` | `AgentLoop::run()` | After creating TurnContext, before running step chain |
| `TurnEnd` | `AgentLoop::run()` | When `should_stop` is true |
| `LLMResponse` | `CallProviderStep` | After LLM call completes (all three paths) |
| `ToolCallStart` | `ExecuteToolsStep` | Before executing each tool call |
| `ToolCallEnd` | `ExecuteToolsStep` | After tool execution completes (with result) |
| `Error` | `AgentLoop::run()` | When step chain returns an error |
| `Interrupt` | `AgentLoop::run()` | When `interrupted_` is detected |

### Steps Emitting Events

Steps that emit events need access to the listener list. This is done by adding an `emit_fn` callback to `TurnContext`:

```cpp
// In TurnContext
std::function<void(const AgentEvent&)> emit_fn;  // Set by AgentLoop
```

`AgentLoop::run()` sets `ctx.emit_fn` to a lambda that calls `emit_event()` on the AgentLoop. Steps call `ctx.emit_fn(event)` when needed.

### CallProviderStep Emission

After LLM response is set on `ctx.response`, in all three paths (streaming, degraded, non-streaming):

```cpp
if (ctx.emit_fn) {
    AgentEvent event;
    event.type = AgentEventType::LLMResponse;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;
    event.assistant_output = ctx.response.content;
    event.usage = ctx.response.usage;
    ctx.emit_fn(event);
}
```

### ExecuteToolsStep Emission

Before each tool execution:

```cpp
if (ctx.emit_fn) {
    AgentEvent event;
    event.type = AgentEventType::ToolCallStart;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;
    event.tool_name = tc.name;
    event.tool_arguments = tc.arguments;
    ctx.emit_fn(event);
}
```

After tool execution:

```cpp
if (ctx.emit_fn) {
    AgentEvent event;
    event.type = AgentEventType::ToolCallEnd;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;
    event.tool_name = tc.name;
    event.tool_result = tool_result.output;
    event.tool_error = tool_result.is_error;
    ctx.emit_fn(event);
}
```

## TurnContext Extension

```cpp
struct TurnContext {
    // ... existing fields ...
    std::string agent_id;                              // NEW: agent identifier
    std::function<void(const AgentEvent&)> emit_fn;    // NEW: event emission callback
};
```

## Built-in LoggingEventListener

Header-only in `src/agent/LoggingEventListener.h`:

```cpp
#pragma once
#include "IEventListener.h"
#include "common/io/Logger.h"

namespace ea::agent {

class LoggingEventListener : public IEventListener {
public:
    void on_event(const AgentEvent& event) override {
        std::string aid = event.agent_id.empty() ? "" : " " + event.agent_id;
        switch (event.type) {
        case AgentEventType::TurnStart:
            EA_INFO("[agent{}] Turn {} start: {}", aid, event.iteration,
                    event.user_input.substr(0, 80));
            break;
        case AgentEventType::ToolCallStart:
            EA_INFO("[agent{}] Tool call: {}", aid, event.tool_name);
            break;
        case AgentEventType::ToolCallEnd:
            EA_DEBUG("[agent{}] Tool result: {} ({} bytes, error={})",
                     aid, event.tool_name, event.tool_result.size(), event.tool_error);
            break;
        case AgentEventType::Error:
            EA_ERROR("[agent{}] Error: {}", aid, event.error_message);
            break;
        case AgentEventType::Interrupt:
            EA_WARN("[agent{}] Interrupted at iteration {}", aid, event.iteration);
            break;
        default:
            break;
        }
    }
};

}  // namespace ea::agent
```

## main.cpp Integration

```cpp
// 8.5. Add event listeners
if (debug) {
    loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
}
```

Only adds the logging listener in `--debug` mode. Normal mode has zero event overhead.

## Subagent Compatibility

`Subagent::execute()` creates `AgentLoop` without listeners. Subagents do not emit events by default. Future enhancement: `SubagentOrchestrator` can inject listeners into subagents if needed.

The `agent_id` field on `TurnContext` is set by `Subagent::execute()` to the subagent's ID, enabling event source identification when listeners are added.

## Synchronous Dispatch Contract

Listeners are called synchronously in the AgentLoop's thread. **Listeners must not block.** If a listener needs asynchronous processing (e.g., WebSocket push), it should enqueue the event internally and process it on a separate thread.

## Config

No TOML configuration needed. Listener registration is programmatic via `add_listener()`. `LoggingEventListener` is controlled by the existing `--debug` CLI flag.

## Test Strategy

### Test Cases

| # | Case | Tag | Description |
|---|------|-----|-------------|
| 1 | Add and remove listeners | `[event]` | `add_listener` / `remove_listener` basic operations |
| 2 | TurnStart event | `[event]` | Verify event received at iteration start with user_input |
| 3 | TurnEnd event | `[event]` | Verify event received when should_stop with assistant_output |
| 4 | LLMResponse event | `[event]` | Verify event emitted after CallProviderStep with usage |
| 5 | ToolCallStart + ToolCallEnd events | `[event]` | Verify events emitted before/after tool execution |
| 6 | Error event | `[event]` | Verify event emitted when step chain errors |
| 7 | Interrupt event | `[event]` | Verify event emitted on interrupt |
| 8 | Multiple listeners all receive events | `[event]` | Register 2 listeners, verify both receive events |
| 9 | No listeners — zero overhead | `[event]` | Full AgentLoop run without listeners, no crashes |

### Mock Listener

```cpp
class MockEventListener : public IEventListener {
public:
    void on_event(const AgentEvent& event) override {
        events_.push_back(event);
    }
    const std::vector<AgentEvent>& events() const { return events_; }
private:
    std::vector<AgentEvent> events_;
};
```

### Test File

New file: `tests/test_events.cpp`, tagged `[event]`.

## Files Changed

| File | Change |
|------|--------|
| `src/agent/AgentEvent.h` | New: event type enum + AgentEvent struct |
| `src/agent/IEventListener.h` | New: listener interface |
| `src/agent/LoggingEventListener.h` | New: built-in logging listener (header-only) |
| `src/agent/AgentLoop.h` | Add `listeners_`, `add_listener()`, `remove_listener()`, `emit()`, `emit_event()` |
| `src/agent/AgentLoop.cpp` | Emit TurnStart/TurnEnd/Error/Interrupt, set `ctx.emit_fn` |
| `src/agent/TurnContext.h` | Add `agent_id`, `emit_fn` fields |
| `src/agent/steps/CallProviderStep.cpp` | Emit LLMResponse event |
| `src/agent/steps/ExecuteToolsStep.cpp` | Emit ToolCallStart/End events |
| `src/main.cpp` | Add LoggingEventListener in debug mode |
| `tests/test_events.cpp` | New: 9 test cases |
| `tests/CMakeLists.txt` | Add test_events.cpp |
