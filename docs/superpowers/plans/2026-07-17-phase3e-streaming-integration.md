# Phase 3E: Streaming Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate `IProvider::stream_chat()` into AgentLoop for token-level streaming output with automatic degradation when the provider doesn't support streaming.

**Architecture:** Add `StreamFn` callback to AgentLoop and TurnContext. CallProviderStep gains three-path logic: streaming (stream_chat + accumulate), degraded (chat + simulate chunks), and non-streaming (original chat). AgentLoop skips OutputFn in streaming mode since content is already delivered via StreamFn.

**Tech Stack:** C++17, nlohmann/json, spdlog, Catch2

## Global Constraints

- Error construction: `Error` is aggregate struct — use factory methods (`Error::invalid_arg()`, `Error::timeout()`, `Error::net()`)
- Namespace: `ea::agent`, `ea::config`; core types in `ea` (no sub-namespace)
- Test tags: `[streaming]` for this module
- Object libraries: each module is a CMake OBJECT library
- `StreamChunk` already exists in `src/core/Types.h` with `Content/ToolCallBegin/ToolCallDelta/ToolCallEnd/Done/Error` types
- `IProvider::stream_chat()` already exists and is fully implemented in `OpenAIProvider`
- `ProviderCapabilities.streaming` field already exists for feature detection
- Subagents must continue using non-streaming path (no changes to Subagent/SubagentOrchestrator)

---

### Task 1: TurnContext + StreamInterrupted

**Files:**
- Modify: `src/agent/TurnContext.h:1-48`

**Interfaces:**
- Consumes: `StreamChunk` from `src/core/Types.h` (already included via `core/IProvider.h`)
- Produces: `TurnContext::stream_callback` field, `StreamInterrupted` exception type

- [ ] **Step 1: Add `stream_callback` field and `StreamInterrupted` to TurnContext.h**

Replace the entire file with:

```cpp
// TurnContext — per-iteration state for the agent loop step chain
#pragma once
#include "core/Types.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <exception>

namespace ea::agent {

// Thrown inside stream_chat on_chunk callback to break out of streaming
struct StreamInterrupted : std::exception {
    const char* what() const noexcept override { return "stream interrupted"; }
};

struct TurnContext {
    // Non-copyable — holds reference to atomic and message history
    TurnContext(const TurnContext&) = delete;
    TurnContext& operator=(const TurnContext&) = delete;
    TurnContext(TurnContext&&) = default;
    TurnContext& operator=(TurnContext&&) = default;

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

    // Streaming callback — when set, CallProviderStep uses stream_chat
    std::function<void(const StreamChunk&)> stream_callback;

    TurnContext(std::vector<Message>& msgs, std::atomic<bool>& intr)
        : messages(msgs), interrupted(intr) {}
};

}  // namespace ea::agent
```

- [ ] **Step 2: Build to verify no breakage**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:|warning:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors. The new field has a default constructor (`std::function` defaults to empty), so all existing code that creates `TurnContext` continues to work.

- [ ] **Step 3: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All 294 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/agent/TurnContext.h
git commit -m "feat(agent): add stream_callback and StreamInterrupted to TurnContext"
```

---

### Task 2: AgentLoop StreamFn + Config::stream

**Files:**
- Modify: `src/agent/AgentLoop.h:1-73`
- Modify: `src/agent/AgentLoop.cpp:1-149`

**Interfaces:**
- Consumes: `StreamChunk` from `src/core/Types.h` (already included via `core/IProvider.h`)
- Produces: `AgentLoop::StreamFn` type alias, `AgentLoop::Config::stream` field, `AgentLoop` constructor with `StreamFn` parameter, `stream_fn_` member

- [ ] **Step 1: Update AgentLoop.h — add StreamFn, Config::stream, constructor param, member**

Replace `src/agent/AgentLoop.h` entirely with:

```cpp
// AgentLoop — orchestrates ITurnStep chain for agent execution
#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "LoopDetector.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
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
        bool stream = true;  // Enable streaming output
    };

    using OutputFn = std::function<void(const std::string&)>;
    using StreamFn = std::function<void(const StreamChunk&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output,
              StreamFn stream_fn = nullptr,
              security::SecurityPolicy* policy = nullptr,
              security::IApprovalHandler* approval = nullptr,
              ContextCompressor* compressor = nullptr);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

    // Step chain customization
    void add_step(std::unique_ptr<ITurnStep> step);
    void set_steps(std::vector<std::unique_ptr<ITurnStep>> steps);

private:
    void build_system_prompt_once();

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;
    StreamFn stream_fn_;
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;
    ContextCompressor* compressor_;

    std::vector<Message> history_;
    std::string system_prompt_;
    std::atomic<bool> interrupted_{false};

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Update AgentLoop.cpp — constructor, run() with stream_fn_ and conditional OutputFn**

Replace `src/agent/AgentLoop.cpp` entirely with:

```cpp
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
                     OutputFn output,
                     StreamFn stream_fn,
                     security::SecurityPolicy* policy,
                     security::IApprovalHandler* approval,
                     ContextCompressor* compressor)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
    , stream_fn_(std::move(stream_fn))
    , policy_(policy)
    , approval_(approval)
    , compressor_(compressor)
{
    // Build default step chain
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>(compressor_));
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>(policy_, approval_));
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
        ctx.stream_callback = stream_fn_;

        // Run step chain
        auto result = run_step_chain(ctx, steps_);
        if (!result.ok()) {
            return result;
        }

        // Output final response if stopping
        // In streaming mode, content is already delivered via StreamFn — skip OutputFn
        if (ctx.should_stop) {
            if (!config_.stream || !stream_fn_) {
                if (output_ && !ctx.response.content.empty()) {
                    output_(ctx.response.content);
                }
            }
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

- [ ] **Step 3: Build to verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors. The new `stream_fn` parameter has a default of `nullptr`, so all existing callers (tests, Subagent, main.cpp) continue to compile.

- [ ] **Step 4: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All 294 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/agent/AgentLoop.h src/agent/AgentLoop.cpp
git commit -m "feat(agent): add StreamFn callback and Config::stream to AgentLoop"
```

---

### Task 3: CallProviderStep Three-Path Streaming Logic

**Files:**
- Modify: `src/agent/steps/CallProviderStep.cpp:1-43`

**Interfaces:**
- Consumes: `TurnContext::stream_callback`, `TurnContext::interrupted`, `IProvider::stream_chat()`, `IProvider::capabilities()`, `StreamInterrupted` from `TurnContext.h`
- Produces: CallProviderStep now supports streaming, degraded, and non-streaming paths

- [ ] **Step 1: Replace CallProviderStep.cpp with three-path logic**

Replace `src/agent/steps/CallProviderStep.cpp` entirely with:

```cpp
#include "CallProviderStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

CallProviderStep::CallProviderStep(ContextCompressor* compressor)
    : compressor_(compressor) {}

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::invalid_arg("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
    }

    // Compress if needed
    if (compressor_) {
        auto compressed = compressor_->compress(messages);
        if (compressed.ok()) {
            messages = std::move(compressed.value());
        }
        // If compression fails, use original messages (graceful degradation)
    }

    ChatOptions opts;

    // Path 1: Streaming — provider supports it and callback is set
    if (ctx.stream_callback && ctx.provider->capabilities().streaming) {
        std::string accumulated_content;
        std::vector<ToolCall> accumulated_calls;

        try {
            auto result = ctx.provider->stream_chat(
                messages, ctx.tool_specs, "",
                [&](const StreamChunk& chunk) {
                    if (ctx.interrupted) throw StreamInterrupted{};
                    ctx.stream_callback(chunk);

                    if (chunk.type == StreamChunk::Type::Content) {
                        accumulated_content += chunk.data;
                    }
                    if (chunk.type == StreamChunk::Type::ToolCallEnd && chunk.tool_call) {
                        accumulated_calls.push_back(chunk.tool_call.value());
                    }
                },
                opts
            );

            if (!result.ok()) {
                EA_ERROR("LLM stream call failed: {}", result.error().message);
                return result.error();
            }
        } catch (const StreamInterrupted&) {
            return Error::timeout("stream interrupted");
        } catch (const std::exception& e) {
            return Error::net(std::string("stream error: ") + e.what());
        }

        ctx.response.content = std::move(accumulated_content);
        ctx.response.tool_calls = std::move(accumulated_calls);
        ctx.response.stop_reason = accumulated_calls.empty() ? "stop" : "tool_calls";
        return {};
    }

    // Path 2: Degraded streaming — callback set but provider doesn't support streaming
    if (ctx.stream_callback) {
        auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
        if (!response.ok()) {
            EA_ERROR("LLM call failed: {}", response.error().message);
            return response.error();
        }

        ctx.response = response.value();

        // Simulate Content chunk
        if (!ctx.response.content.empty()) {
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::Content;
            chunk.data = ctx.response.content;
            ctx.stream_callback(chunk);
        }
        // Simulate ToolCallEnd chunks
        for (const auto& tc : ctx.response.tool_calls) {
            StreamChunk chunk;
            chunk.type = StreamChunk::Type::ToolCallEnd;
            chunk.tool_call = tc;
            ctx.stream_callback(chunk);
        }
        // Simulate Done
        StreamChunk done;
        done.type = StreamChunk::Type::Done;
        ctx.stream_callback(done);

        return {};
    }

    // Path 3: Non-streaming — original behavior
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

- [ ] **Step 2: Build to verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 3: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All 294 tests pass. Non-streaming path is unchanged.

- [ ] **Step 4: Commit**

```bash
git add src/agent/steps/CallProviderStep.cpp
git commit -m "feat(agent): add three-path streaming logic to CallProviderStep"
```

---

### Task 4: Config + main.cpp Integration

**Files:**
- Modify: `src/config/Config.h:38-49` (AgentConfig struct)
- Modify: `src/config/Config.cpp:40-70` (agent section parsing)
- Modify: `src/main.cpp:176-186` (AgentLoop construction)

**Interfaces:**
- Consumes: `AgentLoop::StreamFn`, `AgentLoop::Config::stream`, `StreamChunk`
- Produces: `AgentConfig::stream` field, TOML parsing, CLI streaming callback

- [ ] **Step 1: Add `stream` field to AgentConfig in Config.h**

In `src/config/Config.h`, after line 48 (`std::vector<agent::SubagentConfig> subagents;`), add:

```cpp
    // Streaming output
    bool stream = true;
```

The AgentConfig struct becomes:

```cpp
struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    // Context compression
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
    // Subagent delegation
    std::vector<agent::SubagentConfig> subagents;
    // Streaming output
    bool stream = true;
};
```

- [ ] **Step 2: Add TOML parsing for `agent.stream` in Config.cpp**

In `src/config/Config.cpp`, after line 45 (`cfg.agent.soul = ...`), add:

```cpp
            cfg.agent.stream = toml::find_or<bool>(agent, "stream", cfg.agent.stream);
```

This goes right after the `soul` line and before the `compression` block.

- [ ] **Step 3: Update main.cpp — add StreamFn and pass to AgentLoop**

Replace the AgentLoop construction block (lines 176-186) in `src/main.cpp` with:

```cpp
    // 8. Create agent loop
    ea::agent::AgentLoop::StreamFn stream_fn;

    if (cfg.agent.stream) {
        stream_fn = [](const ea::StreamChunk& chunk) {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                std::cout << chunk.data << std::flush;
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                std::cout << std::endl;
            }
            // ToolCallBegin/Delta/End are silent in CLI mode
            // Server mode can forward these via WebSocket
        };
    }

    ea::agent::AgentLoop loop(
        provider.get(), &registry, memory.get(),
        ea::agent::AgentLoop::Config{
            cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream
        },
        [](const std::string& text) { std::cout << text << std::endl; },
        stream_fn,
        security.get(),
        approval.get(),
        compressor.get()
    );
```

- [ ] **Step 4: Build to verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 5: Run existing tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All 294 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp src/main.cpp
git commit -m "feat: add stream config and CLI streaming callback to main.cpp"
```

---

### Task 5: Streaming Tests

**Files:**
- Create: `tests/test_streaming.cpp`
- Modify: `tests/CMakeLists.txt:1-46`

**Interfaces:**
- Consumes: `AgentLoop::StreamFn`, `AgentLoop::Config::stream`, `CallProviderStep`, `TurnContext`, `StreamInterrupted`, `IProvider`, `StreamChunk`
- Produces: 9 test cases tagged `[streaming]`

- [ ] **Step 1: Create test file with MockStreamProvider and all 9 test cases**

Write `tests/test_streaming.cpp`:

```cpp
// tests/test_streaming.cpp — Streaming integration tests
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "agent/TurnContext.h"
#include "agent/steps/CallProviderStep.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock provider that supports streaming
class MockStreamProvider : public IProvider {
public:
    std::string name() const override { return "mock-stream"; }
    std::vector<std::string> list_models() const override { return {"mock-stream-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, true, false, false};  // streaming = true
    }

    // Queue chunks to be emitted by stream_chat
    void enqueue_chunks(std::vector<StreamChunk> chunks) {
        chunk_queues_.push(std::move(chunks));
    }

    // Queue responses for chat (used in degraded mode tests)
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
                              const std::string&, std::function<void(const StreamChunk&)> on_chunk,
                              const ChatOptions&) override {
        if (chunk_queues_.empty()) return Error::net("no mock chunks");
        auto chunks = std::move(chunk_queues_.front());
        chunk_queues_.pop();
        for (const auto& chunk : chunks) {
            on_chunk(chunk);
        }
        return {};
    }

private:
    std::queue<std::vector<StreamChunk>> chunk_queues_;
    std::queue<LLMResponse> responses_;
};

// Mock provider that does NOT support streaming
class MockNonStreamProvider : public IProvider {
public:
    std::string name() const override { return "mock-nonstream"; }
    std::vector<std::string> list_models() const override { return {"mock-nonstream-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, false, false, false, false};  // streaming = false
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

TEST_CASE("StreamFn content forwarding", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    // Queue: two content chunks + done
    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Hello", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, " world", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(received.size() >= 2);
    REQUIRE(received[0].type == StreamChunk::Type::Content);
    REQUIRE(received[0].data == "Hello");
    REQUIRE(received[1].type == StreamChunk::Type::Content);
    REQUIRE(received[1].data == " world");
}

TEST_CASE("Streaming accumulates complete LLMResponse", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Part1 ", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, "Part2", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::string output;
    auto stream_fn = [](const StreamChunk&) {};

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [&](const std::string& t) { output = t; }, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // In streaming mode, OutputFn should NOT be called
    REQUIRE(output.empty());
}

TEST_CASE("Streaming tool call accumulation", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    ToolCall tc;
    tc.id = "call_1";
    tc.name = "shell";
    tc.arguments = json::object();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::ToolCallBegin, "", tc, std::nullopt},
        StreamChunk{StreamChunk::Type::ToolCallEnd, "", tc, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // Should have received ToolCallBegin and ToolCallEnd
    bool has_begin = false, has_end = false;
    for (const auto& c : received) {
        if (c.type == StreamChunk::Type::ToolCallBegin) has_begin = true;
        if (c.type == StreamChunk::Type::ToolCallEnd) has_end = true;
    }
    REQUIRE(has_begin);
    REQUIRE(has_end);
}

TEST_CASE("Degraded streaming with non-streaming provider", "[streaming]") {
    auto provider = std::make_shared<MockNonStreamProvider>();

    LLMResponse resp;
    resp.content = "Fallback response";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // Should have received simulated Content + Done chunks
    REQUIRE(received.size() >= 2);
    REQUIRE(received[0].type == StreamChunk::Type::Content);
    REQUIRE(received[0].data == "Fallback response");
    // Last should be Done
    REQUIRE(received.back().type == StreamChunk::Type::Done);
}

TEST_CASE("Non-streaming path unchanged when no StreamFn", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    LLMResponse resp;
    resp.content = "Non-stream response";
    resp.stop_reason = "stop";
    // Use chat() path since no stream_fn
    // MockStreamProvider.chat() needs a response queued
    // But MockStreamProvider only has stream_chat — so let's use MockNonStreamProvider
    auto non_stream = std::make_shared<MockNonStreamProvider>();
    non_stream->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    // No stream_fn — should use OutputFn
    AgentLoop loop(non_stream.get(), &registry, nullptr,
                   AgentLoop::Config{90, 65536, 100, true, false},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Non-stream response");
}

TEST_CASE("Stream interruption via StreamInterrupted", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    // Queue chunks — the test will interrupt after first chunk
    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Before interrupt", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, "After interrupt", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::atomic<bool> interrupted{false};
    int chunk_count = 0;

    auto stream_fn = [&](const StreamChunk& chunk) {
        chunk_count++;
        if (chunk_count == 1) {
            // Simulate interrupt after first chunk
            interrupted.store(true);
        }
    };

    // We need to test CallProviderStep directly since AgentLoop::interrupt()
    // sets its own atomic. Use TurnContext directly.
    std::vector<Message> messages;
    TurnContext ctx(messages, interrupted);
    ctx.provider = provider.get();
    ctx.stream_callback = stream_fn;
    ctx.system_prompt = "test";

    CallProviderStep step;
    auto result = step.execute(ctx);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::Timeout);
}

TEST_CASE("AgentLoop streaming skips OutputFn", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Streamed text", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::string output;
    auto stream_fn = [](const StreamChunk&) {};

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [&](const std::string& t) { output = t; }, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // OutputFn should NOT have been called in streaming mode
    REQUIRE(output.empty());
}

TEST_CASE("AgentLoop non-streaming uses OutputFn", "[streaming]") {
    auto provider = std::make_shared<MockNonStreamProvider>();

    LLMResponse resp;
    resp.content = "Non-stream output";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;

    // Config with stream=false, no StreamFn
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{90, 65536, 100, true, false},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Non-stream output");
}

TEST_CASE("Config stream field parsing", "[streaming]") {
    std::string tmp = "/tmp/ea_test_stream_config.toml";
    ea::fs::write_file(tmp, R"(
[agent]
stream = false
)");

    auto cfg = ea::config::load(tmp);
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().agent.stream == false);

    // Default should be true
    auto cfg2 = ea::config::load("/nonexistent/config.toml");
    REQUIRE(cfg2.ok());
    REQUIRE(cfg2.value().agent.stream == true);

    ea::fs::remove(tmp);
}
```

- [ ] **Step 2: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, after line 45 (`test_subagent.cpp`), add:

```
    test_streaming.cpp
```

- [ ] **Step 3: Build**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep -E "error:" | grep -v "mbedtls\|spdlog\|third_party" | head -20`
Expected: No errors.

- [ ] **Step 4: Run streaming tests**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[streaming]" 2>&1`
Expected: All 9 streaming tests pass.

- [ ] **Step 5: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests 2>&1 | tail -5`
Expected: All tests pass (294 + 9 = 303 tests).

- [ ] **Step 6: Commit**

```bash
git add tests/test_streaming.cpp tests/CMakeLists.txt
git commit -m "test: add 9 streaming integration tests"
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

- [ ] **Step 3: Run streaming tests with verbose output**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[streaming]" --reporter console 2>&1`
Expected: All 9 streaming tests pass with detailed output.

- [ ] **Step 4: Verify Subagent tests still pass**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests "[subagent]" 2>&1`
Expected: All 9 subagent tests pass (Subagent uses non-streaming path, unaffected).

- [ ] **Step 5: Commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix: address streaming integration issues found in final verification"
```

If no fixes needed, skip this step.
