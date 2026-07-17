# Phase 3F: Event/Hook System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an event system to AgentLoop that emits lifecycle events (TurnStart/End, ToolCallStart/End, LLMResponse, Error, Interrupt) via IEventListener interface.

**Architecture:** Define AgentEvent struct and IEventListener interface. AgentLoop holds a listener list and emits events synchronously at key lifecycle points. Steps emit events through an `emit_fn` callback on TurnContext. Built-in LoggingEventListener for debug mode.

**Tech Stack:** C++17, nlohmann/json, spdlog, Catch2

## Global Constraints

- Error construction: `Error` is aggregate struct — use factory methods (`Error::invalid_arg()`, `Error::timeout()`)
- Namespace: `ea::agent`; core types in `ea` (no sub-namespace)
- Test tags: `[event]` for this module
- Object libraries: each module is a CMake OBJECT library
- `AgentLoop::Config` has 5 fields: `max_iterations, max_tool_output_bytes, max_messages, auto_memory, stream`
- `TurnContext` already has `stream_callback` and `StreamInterrupted` (from Phase 3E)
- `CallProviderStep` has three-path logic (streaming/degraded/non-streaming)
- `ExecuteToolsStep` iterates `ctx.pending_tool_calls` with security + approval checks
- Subagents must continue working without changes (no listeners added by default)

---

### Task 1: AgentEvent + IEventListener Types

**Files:**
- Create: `src/agent/AgentEvent.h`
- Create: `src/agent/IEventListener.h`

**Interfaces:**
- Consumes: `core/Types.h` (for `json`, `Usage`)
- Produces: `AgentEventType` enum, `AgentEvent` struct, `IEventListener` interface

- [ ] **Step 1: Create AgentEvent.h**

```cpp
// AgentEvent — lifecycle event types for the agent loop
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

- [ ] **Step 2: Create IEventListener.h**

```cpp
// IEventListener — interface for agent lifecycle event consumers
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

- [ ] **Step 3: Build to verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors. New headers are standalone, not yet included by anything.

- [ ] **Step 4: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -3`
Expected: All 303 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/agent/AgentEvent.h src/agent/IEventListener.h
git commit -m "feat(agent): add AgentEvent and IEventListener types"
```

---

### Task 2: TurnContext Extension + AgentLoop Listener Management

**Files:**
- Modify: `src/agent/TurnContext.h:1-58`
- Modify: `src/agent/AgentLoop.h:1-78`
- Modify: `src/agent/AgentLoop.cpp:1-155`

**Interfaces:**
- Consumes: `IEventListener` from Task 1, `AgentEvent` from Task 1
- Produces: `TurnContext::agent_id`, `TurnContext::emit_fn`, `AgentLoop::add_listener()`, `AgentLoop::remove_listener()`, `AgentLoop::emit()`, `AgentLoop::emit_event()`, `AgentLoop::listeners_`

- [ ] **Step 1: Add `agent_id` and `emit_fn` to TurnContext.h**

Add two fields after `stream_callback` (before the constructor):

```cpp
    // Agent identifier (empty for main agent, set for subagents)
    std::string agent_id;

    // Event emission callback — set by AgentLoop for step-level events
    std::function<void(const AgentEvent&)> emit_fn;
```

- [ ] **Step 2: Add listener management to AgentLoop.h**

Add `#include "IEventListener.h"` to the includes.

Add public methods after `set_steps()`:

```cpp
    // Event listener management
    void add_listener(std::shared_ptr<IEventListener> listener);
    void remove_listener(const std::shared_ptr<IEventListener>& listener);
```

Add private methods after `build_system_prompt_once()`:

```cpp
    void emit(AgentEventType type, const TurnContext& ctx);
    void emit_event(const AgentEvent& event);
```

Add private member after `loop_detector_`:

```cpp
    std::vector<std::shared_ptr<IEventListener>> listeners_;
```

- [ ] **Step 3: Implement listener management and emit helpers in AgentLoop.cpp**

Add `#include "AgentEvent.h"` to the includes.

Add these method implementations at the end of the file (before the namespace closing):

```cpp
void AgentLoop::add_listener(std::shared_ptr<IEventListener> listener) {
    listeners_.push_back(std::move(listener));
}

void AgentLoop::remove_listener(const std::shared_ptr<IEventListener>& listener) {
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
    }
}

void AgentLoop::emit(AgentEventType type, const TurnContext& ctx) {
    if (listeners_.empty()) return;

    AgentEvent event;
    event.type = type;
    event.iteration = ctx.iteration;
    event.agent_id = ctx.agent_id;

    switch (type) {
    case AgentEventType::TurnStart:
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
    case AgentEventType::Interrupt:
        break;
    default:
        break;
    }

    emit_event(event);
}

void AgentLoop::emit_event(const AgentEvent& event) {
    for (const auto& listener : listeners_) {
        listener->on_event(event);
    }
}
```

Add `#include <algorithm>` to the includes (for `std::find`).

- [ ] **Step 4: Add event emission points in AgentLoop::run()**

Modify `AgentLoop::run()` to:

1. Set `ctx.emit_fn` when creating TurnContext
2. Emit `TurnStart` before running step chain
3. Emit `TurnEnd` when `should_stop`
4. Emit `Error` when step chain fails
5. Emit `Interrupt` when interrupted

Replace the for-loop body in `AgentLoop::run()` with:

```cpp
    for (int i = 0; i < config_.max_iterations; ++i) {
        if (interrupted_) {
            EA_WARN("Agent loop interrupted at iteration {}", i);
            emit(AgentEventType::Interrupt, TurnContext(history_, interrupted_));
            return Error::timeout("agent loop interrupted");
        }

        // Create turn context
        TurnContext ctx(history_, interrupted_);
        ctx.iteration = i;
        ctx.max_iterations = config_.max_iterations;
        ctx.max_tool_output_bytes = config_.max_tool_output_bytes;
        ctx.provider = provider_;
        ctx.registry = registry_;
        ctx.system_prompt = system_prompt_;
        ctx.stream_callback = stream_fn_;
        ctx.emit_fn = [this](const AgentEvent& e) { emit_event(e); };

        // Emit TurnStart
        emit(AgentEventType::TurnStart, ctx);

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            AgentEvent err_event;
            err_event.type = AgentEventType::Error;
            err_event.iteration = ctx.iteration;
            err_event.agent_id = ctx.agent_id;
            err_event.error_message = result.error().message;
            emit_event(err_event);
            return result;
        }

        // Output final response if stopping
        // In streaming mode, content is already delivered via StreamFn — skip OutputFn
        if (ctx.should_stop) {
            emit(AgentEventType::TurnEnd, ctx);
            if (!config_.stream || !stream_fn_) {
                if (output_ && !ctx.response.content.empty()) {
                    output_(ctx.response.content);
                }
            }
            return {};
        }
    }
```

Note: The Interrupt emit creates a temporary TurnContext just for the emit call — this is acceptable since `emit()` only reads `iteration` and `agent_id` from it, and the history reference won't be accessed for Interrupt type.

- [ ] **Step 5: Build**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 6: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -3`
Expected: All 303 tests pass. No listeners are registered by default, so all existing code is unaffected.

- [ ] **Step 7: Commit**

```bash
git add src/agent/TurnContext.h src/agent/AgentLoop.h src/agent/AgentLoop.cpp
git commit -m "feat(agent): add event listener management and emission to AgentLoop"
```

---

### Task 3: Step-Level Event Emission (CallProviderStep + ExecuteToolsStep)

**Files:**
- Modify: `src/agent/steps/CallProviderStep.cpp:1-115`
- Modify: `src/agent/steps/ExecuteToolsStep.cpp:1-84`

**Interfaces:**
- Consumes: `TurnContext::emit_fn`, `AgentEvent`, `AgentEventType` from Tasks 1-2
- Produces: LLMResponse events from CallProviderStep, ToolCallStart/End events from ExecuteToolsStep

- [ ] **Step 1: Add LLMResponse event emission to CallProviderStep.cpp**

Add `#include "agent/AgentEvent.h"` to the includes.

After each path sets `ctx.response`, add event emission. Specifically:

**After Path 1 (streaming) sets ctx.response (before `return {};`):**

```cpp
        ctx.response.content = std::move(accumulated_content);
        ctx.response.tool_calls = std::move(accumulated_calls);
        ctx.response.stop_reason = accumulated_calls.empty() ? "stop" : "tool_calls";

        // Emit LLMResponse event
        if (ctx.emit_fn) {
            AgentEvent event;
            event.type = AgentEventType::LLMResponse;
            event.iteration = ctx.iteration;
            event.agent_id = ctx.agent_id;
            event.assistant_output = ctx.response.content;
            event.usage = ctx.response.usage;
            ctx.emit_fn(event);
        }

        return {};
```

**After Path 2 (degraded) sets ctx.response and simulates chunks (before `return {};`):**

```cpp
        // Simulate Done
        StreamChunk done;
        done.type = StreamChunk::Type::Done;
        ctx.stream_callback(done);

        // Emit LLMResponse event
        if (ctx.emit_fn) {
            AgentEvent event;
            event.type = AgentEventType::LLMResponse;
            event.iteration = ctx.iteration;
            event.agent_id = ctx.agent_id;
            event.assistant_output = ctx.response.content;
            event.usage = ctx.response.usage;
            ctx.emit_fn(event);
        }

        return {};
```

**After Path 3 (non-streaming) sets ctx.response (before `return {};`):**

```cpp
    ctx.response = std::move(response.value());

    // Emit LLMResponse event
    if (ctx.emit_fn) {
        AgentEvent event;
        event.type = AgentEventType::LLMResponse;
        event.iteration = ctx.iteration;
        event.agent_id = ctx.agent_id;
        event.assistant_output = ctx.response.content;
        event.usage = ctx.response.usage;
        ctx.emit_fn(event);
    }

    return {};
```

- [ ] **Step 2: Add ToolCallStart/End event emission to ExecuteToolsStep.cpp**

Add `#include "agent/AgentEvent.h"` to the includes.

**Before tool execution (after approval check, before `// 3. Execute the tool`):**

```cpp
        // Emit ToolCallStart event
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

**After tool execution and truncation (after the `EA_DEBUG` log, before the loop continues):**

```cpp
        // Emit ToolCallEnd event
        if (ctx.emit_fn) {
            AgentEvent event;
            event.type = AgentEventType::ToolCallEnd;
            event.iteration = ctx.iteration;
            event.agent_id = ctx.agent_id;
            event.tool_name = tc.name;
            event.tool_result = ctx.tool_results.back().output;
            event.tool_error = ctx.tool_results.back().is_error;
            ctx.emit_fn(event);
        }
```

- [ ] **Step 3: Build**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 4: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -3`
Expected: All 303 tests pass. `emit_fn` is null by default, so `if (ctx.emit_fn)` guards skip event emission.

- [ ] **Step 5: Commit**

```bash
git add src/agent/steps/CallProviderStep.cpp src/agent/steps/ExecuteToolsStep.cpp
git commit -m "feat(agent): emit LLMResponse and ToolCall events from steps"
```

---

### Task 4: LoggingEventListener + main.cpp Integration

**Files:**
- Create: `src/agent/LoggingEventListener.h`
- Modify: `src/main.cpp:1-222`

**Interfaces:**
- Consumes: `IEventListener`, `AgentEvent`, `AgentEventType` from Tasks 1-2
- Produces: `LoggingEventListener` class, debug mode listener in main.cpp

- [ ] **Step 1: Create LoggingEventListener.h**

```cpp
// LoggingEventListener — built-in listener that logs agent lifecycle events
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

- [ ] **Step 2: Add LoggingEventListener to main.cpp in debug mode**

Add `#include "agent/LoggingEventListener.h"` to the includes in `src/main.cpp`.

After the AgentLoop construction (after line 201, before `// 9. Interactive loop`), add:

```cpp
    // 8.5. Add event listeners
    if (debug) {
        loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }
```

- [ ] **Step 3: Build**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 4: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -3`
Expected: All 303 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/agent/LoggingEventListener.h src/main.cpp
git commit -m "feat(agent): add LoggingEventListener and integrate in debug mode"
```

---

### Task 5: Event System Tests

**Files:**
- Create: `tests/test_events.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IEventListener`, `AgentEvent`, `AgentEventType`, `AgentLoop::add_listener()`, `AgentLoop::remove_listener()`, `IProvider`, `ITool`
- Produces: 9 test cases tagged `[event]`

- [ ] **Step 1: Create test file with all 9 test cases**

Write `tests/test_events.cpp`:

```cpp
// tests/test_events.cpp — Event system integration tests
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "agent/AgentEvent.h"
#include "agent/IEventListener.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/ITool.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock provider for event testing
class MockEventProvider : public IProvider {
public:
    std::string name() const override { return "mock-event"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, false, false, false, false};
    }

    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        if (responses_.empty()) return Error::net("no mock responses");
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

private:
    std::queue<LLMResponse> responses_;
};

// Mock tool for event testing
class MockEventTool : public ITool {
public:
    MockEventTool(std::string n, std::string output)
        : name_(std::move(n)), output_(std::move(output)) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "mock event tool"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return ToolResult{"call_1", output_, false};
    }
private:
    std::string name_;
    std::string output_;
};

// Mock listener that collects events
class MockEventListener : public IEventListener {
public:
    void on_event(const AgentEvent& event) override {
        events_.push_back(event);
    }
    const std::vector<AgentEvent>& events() const { return events_; }
    size_t count_of(AgentEventType type) const {
        size_t c = 0;
        for (const auto& e : events_) if (e.type == type) c++;
        return c;
    }
private:
    std::vector<AgentEvent> events_;
};

// Provider that always errors
class MockErrorProvider : public IProvider {
public:
    std::string name() const override { return "mock-error"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, false, false, false, false};
    }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return Error::net("mock error");
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }
};

TEST_CASE("Add and remove listeners", "[event]") {
    ToolRegistry registry;
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "Hi";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});

    auto listener = std::make_shared<MockEventListener>();
    loop.add_listener(listener);
    loop.run("test");

    // Listener should have received events
    REQUIRE(listener->events().size() > 0);

    // Remove and run again — no new events
    size_t before = listener->events().size();
    loop.remove_listener(listener);
    provider->enqueue_response(LLMResponse{"Hi2", {}, {}, "stop"});
    loop.run("test2");
    REQUIRE(listener->events().size() == before);
}

TEST_CASE("TurnStart event", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "Hello";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener);

    loop.run("What is AI?");
    REQUIRE(listener->count_of(AgentEventType::TurnStart) >= 1);

    // Find the TurnStart event
    bool found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::TurnStart) {
            REQUIRE(e.user_input == "What is AI?");
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("TurnEnd event", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "AI is great";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener);

    loop.run("test");
    REQUIRE(listener->count_of(AgentEventType::TurnEnd) >= 1);

    bool found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::TurnEnd) {
            REQUIRE(e.assistant_output == "AI is great");
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("LLMResponse event", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "Response text";
    resp.stop_reason = "stop";
    resp.usage.input_tokens = 10;
    resp.usage.output_tokens = 5;
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener);

    loop.run("test");
    REQUIRE(listener->count_of(AgentEventType::LLMResponse) >= 1);

    bool found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::LLMResponse) {
            REQUIRE(e.assistant_output == "Response text");
            REQUIRE(e.usage.input_tokens == 10);
            REQUIRE(e.usage.output_tokens == 5);
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("ToolCallStart and ToolCallEnd events", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();

    // First response: request tool call
    LLMResponse resp1;
    resp1.content = "";
    resp1.stop_reason = "tool_use";
    ToolCall tc;
    tc.id = "call_1";
    tc.name = "mock_tool";
    tc.arguments = json::object();
    resp1.tool_calls.push_back(tc);
    provider->enqueue_response(std::move(resp1));

    // Second response: final answer
    LLMResponse resp2;
    resp2.content = "Done";
    resp2.stop_reason = "stop";
    provider->enqueue_response(std::move(resp2));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockEventTool>("mock_tool", "tool output"));

    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{10}, [](const std::string&) {});
    loop.add_listener(listener);

    loop.run("test");
    REQUIRE(listener->count_of(AgentEventType::ToolCallStart) >= 1);
    REQUIRE(listener->count_of(AgentEventType::ToolCallEnd) >= 1);

    // Verify ToolCallStart data
    bool start_found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::ToolCallStart) {
            REQUIRE(e.tool_name == "mock_tool");
            start_found = true;
            break;
        }
    }
    REQUIRE(start_found);

    // Verify ToolCallEnd data
    bool end_found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::ToolCallEnd) {
            REQUIRE(e.tool_name == "mock_tool");
            REQUIRE(e.tool_result == "tool output");
            REQUIRE(e.tool_error == false);
            end_found = true;
            break;
        }
    }
    REQUIRE(end_found);
}

TEST_CASE("Error event", "[event]") {
    auto provider = std::make_shared<MockErrorProvider>();

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener);

    auto result = loop.run("test");
    REQUIRE_FALSE(result.ok());
    REQUIRE(listener->count_of(AgentEventType::Error) >= 1);

    bool found = false;
    for (const auto& e : listener->events()) {
        if (e.type == AgentEventType::Error) {
            REQUIRE_FALSE(e.error_message.empty());
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Interrupt event", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    // Don't enqueue any response — the loop will be interrupted before it can call the provider

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener);

    // Interrupt immediately
    loop.interrupt();

    auto result = loop.run("test");
    REQUIRE_FALSE(result.ok());
    REQUIRE(listener->count_of(AgentEventType::Interrupt) >= 1);
}

TEST_CASE("Multiple listeners all receive events", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "Hello";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    auto listener1 = std::make_shared<MockEventListener>();
    auto listener2 = std::make_shared<MockEventListener>();

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [](const std::string&) {});
    loop.add_listener(listener1);
    loop.add_listener(listener2);

    loop.run("test");
    REQUIRE(listener1->events().size() > 0);
    REQUIRE(listener2->events().size() > 0);
    REQUIRE(listener1->events().size() == listener2->events().size());
}

TEST_CASE("No listeners — zero overhead", "[event]") {
    auto provider = std::make_shared<MockEventProvider>();
    LLMResponse resp;
    resp.content = "Hello";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{1}, [&](const std::string& t) { output = t; });

    // No listeners added — should work normally
    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello");
}
```

- [ ] **Step 2: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, after `test_streaming.cpp`, add:

```
    test_events.cpp
```

- [ ] **Step 3: Build**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 4: Run event tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[event]" 2>&1`
Expected: All 9 event tests pass.

- [ ] **Step 5: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All tests pass (303 + 9 = 312 tests).

- [ ] **Step 6: Commit**

```bash
git add tests/test_events.cpp tests/CMakeLists.txt
git commit -m "test: add 9 event system tests"
```

---

### Task 6: Final Verification

**Files:** None (verification only)

- [ ] **Step 1: Clean rebuild**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) 2>&1 | grep -E "error:|warning:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors, no warnings from project code.

- [ ] **Step 2: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All tests pass.

- [ ] **Step 3: Run event tests with verbose output**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[event]" --reporter console 2>&1`
Expected: All 9 event tests pass with detailed output.

- [ ] **Step 4: Verify streaming and subagent tests still pass**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[streaming]" && ./tests/ea-tests "[subagent]" 2>&1`
Expected: All streaming and subagent tests pass.

- [ ] **Step 5: Commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix: address event system issues found in final verification"
```

If no fixes needed, skip this step.
