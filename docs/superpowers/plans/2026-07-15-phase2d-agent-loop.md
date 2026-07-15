# Phase 2D: Agent Loop Reinforcement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decompose the monolithic AgentLoop into a pluggable ITurnStep chain, add LoopDetector with 3 detection patterns, and replace the interrupt mechanism with atomic flag checked per-step.

**Architecture:** TurnContext holds per-iteration state. ITurnStep defines a single step interface. AgentLoop orchestrates step execution in sequence. LoopDetector checks for exact repeat, ping-pong, and no-progress patterns. Steps are: HistoryPruneStep → BuildToolSpecsStep → CallProviderStep → ParseResponseStep → LoopDetectStep → ExecuteToolsStep → CollectResultsStep.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, Catch2

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/agent/TurnContext.h` | Per-iteration state struct |
| `src/agent/ITurnStep.h` | Step interface |
| `src/agent/LoopDetector.h` | Loop detection declarations |
| `src/agent/LoopDetector.cpp` | 3-pattern detection implementation |
| `src/agent/steps/HistoryPruneStep.h` | Message history pruning step |
| `src/agent/steps/BuildToolSpecsStep.h` | Tool spec assembly step |
| `src/agent/steps/CallProviderStep.h` | LLM invocation step |
| `src/agent/steps/CallProviderStep.cpp` | Implementation |
| `src/agent/steps/ParseResponseStep.h` | Response parsing step |
| `src/agent/steps/ParseResponseStep.cpp` | Implementation |
| `src/agent/steps/LoopDetectStep.h` | Loop detection step |
| `src/agent/steps/LoopDetectStep.cpp` | Implementation |
| `src/agent/steps/ExecuteToolsStep.h` | Tool execution step |
| `src/agent/steps/ExecuteToolsStep.cpp` | Implementation |
| `src/agent/steps/CollectResultsStep.h` | Result collection step |
| `tests/test_turn_context.cpp` | TurnContext tests |
| `tests/test_loop_detector.cpp` | LoopDetector tests |
| `tests/test_turn_steps.cpp` | Step integration tests |

### Modified Files

| File | Change |
|------|--------|
| `src/agent/AgentLoop.h` | Refactor to use step chain, add TurnContext |
| `src/agent/AgentLoop.cpp` | Replace monolithic loop with step orchestration |
| `src/agent/CMakeLists.txt` | Add new source files |
| `tests/test_agent_loop.cpp` | Update to work with refactored AgentLoop |
| `tests/CMakeLists.txt` | Add new test files |

---

## Task 1: TurnContext and ITurnStep

**Files:**
- Create: `src/agent/TurnContext.h`
- Create: `src/agent/ITurnStep.h`
- Create: `tests/test_turn_context.cpp`
- Modify: `src/agent/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write TurnContext header**

```cpp
// src/agent/TurnContext.h
#pragma once
#include "core/Types.h"
#include "tool/ToolRegistry.h"
#include <vector>
#include <string>
#include <atomic>

namespace ea::agent {

struct TurnContext {
    // Message history (shared with AgentLoop)
    std::vector<Message>& messages;

    // Per-iteration state
    std::vector<ToolCall> pending_tool_calls;
    std::vector<ToolResult> tool_results;
    std::vector<ToolSpec> tool_specs;
    LLMResponse response;

    // Iteration tracking
    int iteration = 0;
    int max_iterations = 90;
    int max_tool_output_bytes = 65536;

    // Control flags
    bool should_stop = false;
    std::atomic<bool>& interrupted;

    // Dependencies (non-owning)
    IProvider* provider = nullptr;
    tool::ToolRegistry* registry = nullptr;

    // System prompt (built once, reused)
    std::string system_prompt;

    TurnContext(std::vector<Message>& msgs, std::atomic<bool>& intr)
        : messages(msgs), interrupted(intr) {}
};

}  // namespace ea::agent
```

- [ ] **Step 2: Write ITurnStep header**

```cpp
// src/agent/ITurnStep.h
#pragma once
#include "TurnContext.h"
#include "common/base/Result.h"
#include <string>
#include <memory>
#include <vector>

namespace ea::agent {

class ITurnStep {
public:
    virtual ~ITurnStep() = default;
    virtual Result<void> execute(TurnContext& ctx) = 0;
    virtual std::string name() const = 0;
};

// Run a chain of steps in order, stopping on error or should_stop
Result<void> run_step_chain(TurnContext& ctx, const std::vector<std::unique_ptr<ITurnStep>>& steps);

}  // namespace ea::agent
```

- [ ] **Step 3: Write TurnContext tests**

```cpp
// tests/test_turn_context.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/TurnContext.h"
#include "agent/ITurnStep.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("TurnContext initialization", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    REQUIRE(ctx.messages.empty());
    REQUIRE(ctx.iteration == 0);
    REQUIRE(ctx.should_stop == false);
    REQUIRE(ctx.interrupted == false);
    REQUIRE(ctx.provider == nullptr);
    REQUIRE(ctx.registry == nullptr);
}

TEST_CASE("TurnContext should_stop flag", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.should_stop = true;
    REQUIRE(ctx.should_stop == true);
}

TEST_CASE("TurnContext interrupted atomic flag", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    REQUIRE(ctx.interrupted == false);
    ctx.interrupted = true;
    REQUIRE(ctx.interrupted == true);
}

// Simple test step
class TestStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        ctx.iteration++;
        return {};
    }
    std::string name() const override { return "test_step"; }
};

TEST_CASE("ITurnStep interface", "[agent][turncontext]") {
    TestStep step;
    REQUIRE(step.name() == "test_step");

    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.iteration == 1);
}

TEST_CASE("run_step_chain executes steps in order", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    std::vector<std::unique_ptr<ITurnStep>> steps;
    steps.push_back(std::make_unique<TestStep>());
    steps.push_back(std::make_unique<TestStep>());
    steps.push_back(std::make_unique<TestStep>());

    auto result = run_step_chain(ctx, steps);
    REQUIRE(result.ok());
    REQUIRE(ctx.iteration == 3);
}
```

- [ ] **Step 4: Add run_step_chain implementation to ITurnStep.h (header-only for simplicity)**

Add after the `run_step_chain` declaration:

```cpp
inline Result<void> run_step_chain(TurnContext& ctx, const std::vector<std::unique_ptr<ITurnStep>>& steps) {
    for (const auto& step : steps) {
        if (ctx.interrupted) {
            return Error::timeout("agent loop interrupted");
        }
        if (ctx.should_stop) {
            return {};
        }
        auto result = step->execute(ctx);
        if (!result.ok()) {
            return result;
        }
    }
    return {};
}
```

- [ ] **Step 5: Add files to CMakeLists**

In `tests/CMakeLists.txt`, add `test_turn_context.cpp` to the `ea-tests` source list.

No new .cpp files for the agent library yet (TurnContext.h and ITurnStep.h are header-only).

- [ ] **Step 6: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[turncontext]" --reporter compact`
Expected: All 5 tests pass.

---

## Task 2: LoopDetector

**Files:**
- Create: `src/agent/LoopDetector.h`
- Create: `src/agent/LoopDetector.cpp`
- Create: `tests/test_loop_detector.cpp`
- Modify: `src/agent/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write LoopDetector header**

```cpp
// src/agent/LoopDetector.h
#pragma once
#include "core/Types.h"
#include <deque>
#include <string>
#include <functional>

namespace ea::agent {

enum class LoopAction { Continue, Warn, Block, Break };

class LoopDetector {
public:
    LoopAction check(const ToolCall& call, const ToolResult& result);
    void reset();

    // Configuration
    int exact_repeat_threshold = 3;   // same tool+args N times consecutively
    int ping_pong_threshold = 4;      // two tools alternating for N cycles
    int no_progress_threshold = 5;    // same tool called N times with identical result

private:
    bool is_exact_repeat(const ToolCall& call) const;
    bool is_ping_pong(const ToolCall& call) const;
    bool is_no_progress(const ToolCall& call, const ToolResult& result) const;

    std::string call_signature(const ToolCall& call) const;
    std::string result_signature(const ToolResult& result) const;

    struct CallRecord {
        std::string signature;    // tool_name + args hash
        std::string result_hash;  // result output hash
        std::string tool_name;
    };

    std::deque<CallRecord> recent_calls_;
    int identical_count_ = 0;
    int ping_pong_count_ = 0;
    // Track per-tool call counts for no-progress detection
    std::deque<std::pair<std::string, std::string>> tool_result_history_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Write LoopDetector implementation**

```cpp
// src/agent/LoopDetector.cpp
#include "LoopDetector.h"
#include <algorithm>

namespace ea::agent {

std::string LoopDetector::call_signature(const ToolCall& call) const {
    // Simple signature: tool_name + arguments as string
    return call.name + ":" + call.arguments.dump();
}

std::string LoopDetector::result_signature(const ToolResult& result) const {
    // Simple hash: first 64 chars of output + is_error flag
    std::string sig;
    sig += result.is_error ? "E" : "O";
    sig += result.output.substr(0, 64);
    return sig;
}

LoopAction LoopDetector::check(const ToolCall& call, const ToolResult& result) {
    CallRecord record;
    record.signature = call_signature(call);
    record.result_hash = result_signature(result);
    record.tool_name = call.name;

    // Check patterns before adding to history
    bool exact = is_exact_repeat(call);
    bool pong = is_ping_pong(call);
    bool noprogress = is_no_progress(call, result);

    // Add to history
    recent_calls_.push_back(std::move(record));
    tool_result_history_.push_back({call.name, result_signature(result)});

    // Keep history bounded
    while (recent_calls_.size() > 20) {
        recent_calls_.pop_front();
    }
    while (tool_result_history_.size() > 50) {
        tool_result_history_.pop_front();
    }

    // Escalation: Break > Block > Warn > Continue
    if (noprogress) return LoopAction::Break;
    if (exact) return LoopAction::Block;
    if (pong) return LoopAction::Warn;

    return LoopAction::Continue;
}

bool LoopDetector::is_exact_repeat(const ToolCall& call) const {
    if (recent_calls_.size() < static_cast<size_t>(exact_repeat_threshold - 1)) return false;

    std::string sig = call_signature(call);
    int count = 1;  // current call
    for (auto it = recent_calls_.rbegin();
         it != recent_calls_.rend() && it->signature == sig;
         ++it) {
        count++;
    }
    return count >= exact_repeat_threshold;
}

bool LoopDetector::is_ping_pong(const ToolCall& call) const {
    if (recent_calls_.size() < static_cast<size_t>(ping_pong_threshold - 1)) return false;

    // Check if we're alternating between two tools
    std::string current = call.name;
    int alternations = 0;

    for (auto it = recent_calls_.rbegin(); it != recent_calls_.rend(); ++it) {
        if (it->tool_name != current) {
            alternations++;
            current = it->tool_name;
        } else {
            break;  // consecutive same tool breaks the pattern
        }
    }

    return alternations >= ping_pong_threshold;
}

bool LoopDetector::is_no_progress(const ToolCall& call, const ToolResult& result) const {
    std::string res_sig = result_signature(result);
    int same_count = 0;

    for (auto it = tool_result_history_.rbegin(); it != tool_result_history_.rend(); ++it) {
        if (it->first != call.name) break;
        if (it->second == res_sig) {
            same_count++;
        } else {
            break;
        }
    }

    // +1 for current call
    return (same_count + 1) >= no_progress_threshold;
}

void LoopDetector::reset() {
    recent_calls_.clear();
    tool_result_history_.clear();
    identical_count_ = 0;
    ping_pong_count_ = 0;
}

}  // namespace ea::agent
```

- [ ] **Step 3: Write LoopDetector tests**

```cpp
// tests/test_loop_detector.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/LoopDetector.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("LoopDetector allows normal calls", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "test1"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result1";

    auto action = detector.check(tc, result);
    REQUIRE(action == LoopAction::Continue);
}

TEST_CASE("LoopDetector detects exact repeat", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "same"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // First two calls should be fine
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    // Third identical call triggers Block
    REQUIRE(detector.check(tc, result) == LoopAction::Block);
}

TEST_CASE("LoopDetector detects ping-pong", "[agent][loopdetect]") {
    LoopDetector detector;

    ToolCall tc_a;
    tc_a.id = "1";
    tc_a.name = "tool_a";
    tc_a.arguments = json::object();

    ToolCall tc_b;
    tc_b.id = "2";
    tc_b.name = "tool_b";
    tc_b.arguments = json::object();

    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // Alternating: a, b, a, b, a → 4 alternations → Warn
    detector.check(tc_a, result);
    detector.check(tc_b, result);
    detector.check(tc_a, result);
    detector.check(tc_b, result);
    auto action = detector.check(tc_a, result);
    REQUIRE(action == LoopAction::Warn);
}

TEST_CASE("LoopDetector detects no progress", "[agent][loopdetect]") {
    LoopDetector detector;
    detector.no_progress_threshold = 3;  // Lower for testing

    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "varying"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "same result every time";

    // Same tool, same result, different args
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Break);
}

TEST_CASE("LoopDetector reset clears state", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "same"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // Build up some state
    detector.check(tc, result);
    detector.check(tc, result);
    detector.check(tc, result);

    detector.reset();

    // After reset, same calls should not trigger
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
}

TEST_CASE("LoopDetector different args don't trigger exact repeat", "[agent][loopdetect]") {
    LoopDetector detector;

    ToolCall tc1;
    tc1.id = "1";
    tc1.name = "search";
    tc1.arguments = json{{"query", "query1"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    ToolCall tc2;
    tc2.id = "2";
    tc2.name = "search";
    tc2.arguments = json{{"query", "query2"}};

    REQUIRE(detector.check(tc1, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc2, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc1, result) == LoopAction::Continue);
}
```

- [ ] **Step 4: Add files to CMakeLists**

In `src/agent/CMakeLists.txt`, add `LoopDetector.cpp` to the source list.

In `tests/CMakeLists.txt`, add `test_loop_detector.cpp` to the `ea-tests` source list.

- [ ] **Step 5: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[loopdetect]" --reporter compact`
Expected: All 6 tests pass.

---

## Task 3: Turn Steps (Header-Only)

**Files:**
- Create: `src/agent/steps/HistoryPruneStep.h`
- Create: `src/agent/steps/BuildToolSpecsStep.h`
- Create: `src/agent/steps/CallProviderStep.h`
- Create: `src/agent/steps/CallProviderStep.cpp`
- Create: `src/agent/steps/ParseResponseStep.h`
- Create: `src/agent/steps/LoopDetectStep.h`
- Create: `src/agent/steps/LoopDetectStep.cpp`
- Create: `src/agent/steps/ExecuteToolsStep.h`
- Create: `src/agent/steps/ExecuteToolsStep.cpp`
- Create: `src/agent/steps/CollectResultsStep.h`
- Modify: `src/agent/CMakeLists.txt`

- [ ] **Step 1: Write HistoryPruneStep**

```cpp
// src/agent/steps/HistoryPruneStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class HistoryPruneStep : public ITurnStep {
public:
    explicit HistoryPruneStep(int max_messages = 100) : max_messages_(max_messages) {}

    Result<void> execute(TurnContext& ctx) override {
        if (static_cast<int>(ctx.messages.size()) <= max_messages_) {
            return {};
        }

        // Keep first message (system) and last N messages
        // Remove middle messages, leave a breadcrumb
        int to_remove = static_cast<int>(ctx.messages.size()) - max_messages_;
        if (to_remove > 0 && ctx.messages.size() > 2) {
            // Remove from position 1 (after system) up to to_remove
            auto begin = ctx.messages.begin() + 1;
            auto end = begin + std::min(to_remove, static_cast<int>(ctx.messages.size()) - 2);
            ctx.messages.erase(begin, end);

            // Insert breadcrumb
            Message breadcrumb;
            breadcrumb.role = Role::System;
            breadcrumb.content = "[Earlier conversation history pruned to fit context window]";
            ctx.messages.insert(ctx.messages.begin() + 1, std::move(breadcrumb));
        }

        return {};
    }

    std::string name() const override { return "history_prune"; }

private:
    int max_messages_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Write BuildToolSpecsStep**

```cpp
// src/agent/steps/BuildToolSpecsStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class BuildToolSpecsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        if (ctx.registry) {
            ctx.tool_specs = ctx.registry->active_specs();
        }
        return {};
    }

    std::string name() const override { return "build_tool_specs"; }
};

}  // namespace ea::agent
```

- [ ] **Step 3: Write CallProviderStep header**

```cpp
// src/agent/steps/CallProviderStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class CallProviderStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "call_provider"; }
};

}  // namespace ea::agent
```

- [ ] **Step 4: Write CallProviderStep implementation**

```cpp
// src/agent/steps/CallProviderStep.cpp
#include "CallProviderStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::config("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
    }

    ChatOptions opts;
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) {
        EA_ERROR("LLM call failed: {}", response.error().message);
        return response.error();
    }

    ctx.response = std::move(response.value());
    return {};
}

}  // namespace ea::agent
```

- [ ] **Step 5: Write ParseResponseStep**

```cpp
// src/agent/steps/ParseResponseStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class ParseResponseStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        // Add assistant message to history
        Message assistant_msg{Role::Assistant, ctx.response.content, std::nullopt, std::nullopt, std::nullopt};
        if (!ctx.response.tool_calls.empty()) {
            assistant_msg.tool_calls = ctx.response.tool_calls;
        }
        ctx.messages.push_back(std::move(assistant_msg));

        // If no tool calls, we're done
        if (!ctx.response.is_tool_use() || ctx.response.tool_calls.empty()) {
            ctx.should_stop = true;
        } else {
            ctx.pending_tool_calls = ctx.response.tool_calls;
        }

        return {};
    }

    std::string name() const override { return "parse_response"; }
};

}  // namespace ea::agent
```

- [ ] **Step 6: Write LoopDetectStep header**

```cpp
// src/agent/steps/LoopDetectStep.h
#pragma once
#include "agent/ITurnStep.h"
#include "agent/LoopDetector.h"
#include <functional>

namespace ea::agent {

class LoopDetectStep : public ITurnStep {
public:
    using WarnFn = std::function<void(const std::string&)>;

    explicit LoopDetectStep(LoopDetector& detector, WarnFn warn = nullptr)
        : detector_(detector), warn_(std::move(warn)) {}

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "loop_detect"; }

private:
    LoopDetector& detector_;
    WarnFn warn_;
};

}  // namespace ea::agent
```

- [ ] **Step 7: Write LoopDetectStep implementation**

```cpp
// src/agent/steps/LoopDetectStep.cpp
#include "LoopDetectStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> LoopDetectStep::execute(TurnContext& ctx) {
    // Only check if we have pending tool calls
    if (ctx.pending_tool_calls.empty() || ctx.tool_results.empty()) {
        return {};
    }

    // Check the last tool call + result
    const auto& last_call = ctx.pending_tool_calls.back();
    const auto& last_result = ctx.tool_results.back();

    auto action = detector_.check(last_call, last_result);

    switch (action) {
    case LoopAction::Continue:
        break;
    case LoopAction::Warn:
        EA_WARN("Loop detected: ping-pong pattern");
        if (warn_) warn_("Loop detected: alternating tool calls detected");
        break;
    case LoopAction::Block:
        EA_WARN("Loop detected: exact repeat, blocking tool call");
        ctx.pending_tool_calls.clear();
        ctx.tool_results.clear();
        // Add a warning message to history
        ctx.messages.push_back({Role::System,
            "[System: Repeated identical tool call detected. Breaking loop.]", std::nullopt, std::nullopt, std::nullopt});
        break;
    case LoopAction::Break:
        EA_WARN("Loop detected: no progress, breaking loop");
        ctx.should_stop = true;
        break;
    }

    return {};
}

}  // namespace ea::agent
```

- [ ] **Step 8: Write ExecuteToolsStep header**

```cpp
// src/agent/steps/ExecuteToolsStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class ExecuteToolsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "execute_tools"; }
};

}  // namespace ea::agent
```

- [ ] **Step 9: Write ExecuteToolsStep implementation**

```cpp
// src/agent/steps/ExecuteToolsStep.cpp
#include "ExecuteToolsStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

Result<void> ExecuteToolsStep::execute(TurnContext& ctx) {
    if (ctx.pending_tool_calls.empty()) {
        return {};
    }

    if (!ctx.registry) {
        return Error::config("No tool registry configured");
    }

    ctx.tool_results.clear();

    for (const auto& tc : ctx.pending_tool_calls) {
        EA_DEBUG("Executing tool: {} (id: {})", tc.name, tc.id);

        auto result = ctx.registry->execute(tc.name, tc.arguments);
        ToolResult tool_result;
        if (result.ok()) {
            tool_result = std::move(result.value());
        } else {
            tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
        }

        // Truncate output if too long
        if (static_cast<int>(tool_result.output.size()) > ctx.max_tool_output_bytes) {
            tool_result.output = tool_result.output.substr(0, ctx.max_tool_output_bytes)
                                 + "\n... [truncated]";
        }

        ctx.tool_results.push_back(std::move(tool_result));

        EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                 ctx.tool_results.back().output.size(), ctx.tool_results.back().is_error);
    }

    return {};
}

}  // namespace ea::agent
```

- [ ] **Step 10: Write CollectResultsStep**

```cpp
// src/agent/steps/CollectResultsStep.h
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class CollectResultsStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        for (size_t i = 0; i < ctx.tool_results.size(); ++i) {
            const auto& result = ctx.tool_results[i];
            std::string tool_name;
            std::string call_id;

            // Match result to its tool call
            if (i < ctx.pending_tool_calls.size()) {
                tool_name = ctx.pending_tool_calls[i].name;
                call_id = ctx.pending_tool_calls[i].id;
            }

            Message tool_msg{Role::Tool, result.output, tool_name, std::nullopt, call_id};
            ctx.messages.push_back(std::move(tool_msg));
        }

        // Clear per-iteration state
        ctx.pending_tool_calls.clear();
        ctx.tool_results.clear();

        return {};
    }

    std::string name() const override { return "collect_results"; }
};

}  // namespace ea::agent
```

- [ ] **Step 11: Update CMakeLists.txt**

In `src/agent/CMakeLists.txt`, add the new .cpp files and include path:

```cmake
add_library(ea-agent OBJECT
    AgentLoop.cpp
    SystemPrompt.cpp
    LoopDetector.cpp
    steps/CallProviderStep.cpp
    steps/LoopDetectStep.cpp
    steps/ExecuteToolsStep.cpp
)
ea_target_compile_options(ea-agent)
target_include_directories(ea-agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-agent PUBLIC ea-core ea-common ea-platform ea-tool)
```

- [ ] **Step 12: Build and verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc)`
Expected: Build succeeds with no errors.

---

## Task 4: Refactor AgentLoop to Use Step Chain

**Files:**
- Modify: `src/agent/AgentLoop.h`
- Modify: `src/agent/AgentLoop.cpp`
- Modify: `tests/test_agent_loop.cpp`
- Create: `tests/test_turn_steps.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Rewrite AgentLoop.h**

```cpp
// src/agent/AgentLoop.h
#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "LoopDetector.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

namespace ea::agent {

using ToolRegistry = tool::ToolRegistry;

class AgentLoop {
public:
    struct Config {
        int max_iterations = 90;
        int max_tool_output_bytes = 65536;
        int max_messages = 100;
        bool auto_memory = true;
    };

    using OutputFn = std::function<void(const std::string&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

    // Step chain customization
    void add_step(std::unique_ptr<ITurnStep> step);
    void set_steps(std::vector<std::unique_ptr<ITurnStep>> steps);

private:
    std::vector<Message> build_messages() const;
    void build_system_prompt_once();

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;

    std::vector<Message> history_;
    std::string system_prompt_;
    std::atomic<bool> interrupted_{false};

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Rewrite AgentLoop.cpp**

```cpp
// src/agent/AgentLoop.cpp
#include "AgentLoop.h"
#include "SystemPrompt.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"
#include "steps/HistoryPruneStep.h"
#include "steps/BuildToolSpecsStep.h"
#include "steps/CallProviderStep.h"
#include "steps/ParseResponseStep.h"
#include "steps/LoopDetectStep.h"
#include "steps/ExecuteToolsStep.h"
#include "steps/CollectResultsStep.h"

namespace ea::agent {

AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
{
    // Build default step chain
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>());
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>());
    steps_.push_back(std::make_unique<LoopDetectStep>(loop_detector_));
    steps_.push_back(std::make_unique<CollectResultsStep>());
}

Result<void> AgentLoop::run(const std::string& user_input) {
    interrupted_ = false;
    loop_detector_.reset();

    // Add user message to history
    history_.push_back({Role::User, user_input, std::nullopt, std::nullopt, std::nullopt});

    // Build system prompt once
    build_system_prompt_once();

    for (int i = 0; i < config_.max_iterations; ++i) {
        if (interrupted_) {
            EA_WARN("Agent loop interrupted at iteration {}", i);
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

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            return result;
        }

        // Output final response if stopping
        if (ctx.should_stop && output_ && !ctx.response.content.empty()) {
            output_(ctx.response.content);
        }

        if (ctx.should_stop) {
            return {};
        }
    }

    // Max iterations reached
    EA_WARN("Max iterations ({}) reached", config_.max_iterations);
    if (output_) {
        output_("[Warning: Reached maximum iteration limit]");
    }
    return {};
}

void AgentLoop::build_system_prompt_once() {
    if (!system_prompt_.empty()) return;

    PromptContext ctx;
    ctx.soul = "You are a helpful AI assistant.";
    ctx.platform_info = platform::platform_description();

    // Get tool guidance
    auto specs = registry_->active_specs();
    std::string tool_guide = "Available tools:\n";
    for (const auto& spec : specs) {
        tool_guide += "- " + spec.name + ": " + spec.description + "\n";
    }
    ctx.tool_guidance = tool_guide;

    // Auto-inject relevant memories
    if (memory_ && config_.auto_memory) {
        for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
            if (it->role == Role::User) {
                EA_DEBUG("Querying memory with: {}", it->content);
                auto mem_result = memory_->recall(it->content, 5);
                if (mem_result.ok() && !mem_result.value().empty()) {
                    ctx.relevant_memories = std::move(mem_result.value());
                } else {
                    auto recent = memory_->list(5, 0);
                    if (recent.ok()) {
                        ctx.relevant_memories = std::move(recent.value());
                    }
                }
                break;
            }
        }
    }

    system_prompt_ = build_system_prompt(ctx);
}

void AgentLoop::interrupt() {
    interrupted_ = true;
}

const std::vector<Message>& AgentLoop::history() const {
    return history_;
}

void AgentLoop::clear_history() {
    history_.clear();
    system_prompt_.clear();
}

void AgentLoop::add_step(std::unique_ptr<ITurnStep> step) {
    steps_.push_back(std::move(step));
}

void AgentLoop::set_steps(std::vector<std::unique_ptr<ITurnStep>> steps) {
    steps_ = std::move(steps);
}

}  // namespace ea::agent
```

- [ ] **Step 3: Update test_agent_loop.cpp**

The existing tests should still work since the public API is unchanged. The `registry->get_all_specs()` call in the old test needs to be `registry->active_specs()` if it was using the old API. Let's verify the existing tests compile and pass.

- [ ] **Step 4: Write turn steps integration test**

```cpp
// tests/test_turn_steps.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/TurnContext.h"
#include "agent/ITurnStep.h"
#include "agent/LoopDetector.h"
#include "agent/steps/HistoryPruneStep.h"
#include "agent/steps/BuildToolSpecsStep.h"
#include "agent/steps/ParseResponseStep.h"
#include "agent/steps/CollectResultsStep.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("HistoryPruneStep prunes when over limit", "[agent][steps]") {
    std::vector<Message> msgs;
    // Add system + 5 messages
    msgs.push_back({Role::System, "system", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 5; ++i) {
        msgs.push_back({Role::User, "msg" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    HistoryPruneStep step(4);  // max 4 messages
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    // Should have pruned: system + breadcrumb + remaining messages
    REQUIRE(msgs.size() <= 5);  // system + breadcrumb + at most 3 remaining
}

TEST_CASE("HistoryPruneStep no-op when under limit", "[agent][steps]") {
    std::vector<Message> msgs;
    msgs.push_back({Role::System, "system", std::nullopt, std::nullopt, std::nullopt});
    msgs.push_back({Role::User, "msg1", std::nullopt, std::nullopt, std::nullopt});

    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    HistoryPruneStep step(100);
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(msgs.size() == 2);
}

TEST_CASE("BuildToolSpecsStep with null registry", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    BuildToolSpecsStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.tool_specs.empty());
}

TEST_CASE("ParseResponseStep stops on no tool use", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.response.content = "Hello!";
    ctx.response.stop_reason = "stop";

    ParseResponseStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == true);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Role::Assistant);
}

TEST_CASE("ParseResponseStep continues on tool use", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.response.content = "";
    ctx.response.stop_reason = "tool_use";
    ToolCall tc;
    tc.id = "call_1";
    tc.name = "search";
    tc.arguments = json::object();
    ctx.response.tool_calls.push_back(tc);

    ParseResponseStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == false);
    REQUIRE(ctx.pending_tool_calls.size() == 1);
}

TEST_CASE("CollectResultsStep appends tool results to messages", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ToolCall tc;
    tc.id = "call_1";
    tc.name = "search";
    tc.arguments = json::object();
    ctx.pending_tool_calls.push_back(tc);

    ToolResult tr;
    tr.call_id = "call_1";
    tr.output = "search result";
    tr.is_error = false;
    ctx.tool_results.push_back(tr);

    CollectResultsStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Role::Tool);
    REQUIRE(msgs[0].content == "search result");
    REQUIRE(ctx.pending_tool_calls.empty());
    REQUIRE(ctx.tool_results.empty());
}

TEST_CASE("Step chain with should_stop", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    // A step that sets should_stop
    class StopStep : public ITurnStep {
    public:
        Result<void> execute(TurnContext& c) override {
            c.should_stop = true;
            return {};
        }
        std::string name() const override { return "stop"; }
    };

    std::vector<std::unique_ptr<ITurnStep>> steps;
    steps.push_back(std::make_unique<StopStep>());
    steps.push_back(std::make_unique<ParseResponseStep>());  // should not execute

    auto result = run_step_chain(ctx, steps);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == true);
    // ParseResponseStep should not have run (no assistant message added)
    REQUIRE(msgs.empty());
}
```

- [ ] **Step 5: Add test file to CMakeLists**

In `tests/CMakeLists.txt`, add `test_turn_steps.cpp` to the `ea-tests` source list.

- [ ] **Step 6: Build and run all agent tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[agent]" --reporter compact`
Expected: All agent tests pass (existing + new).

- [ ] **Step 7: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests --reporter compact`
Expected: All tests pass, zero regressions.

---

## Task 5: Final Verification

- [ ] **Step 1: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 2: Verify no compiler warnings**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep "warning:" | grep -v "mbedtls"`
Expected: Zero warnings in ea-* code.
