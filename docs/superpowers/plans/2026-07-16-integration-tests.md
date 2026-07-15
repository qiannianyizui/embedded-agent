# Integration Test Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add end-to-end integration tests that exercise multiple subsystems together (Provider → AgentLoop → Tool → Memory), verifying cross-module contracts that unit tests cannot catch.

**Architecture:** Integration tests use MockProvider (pre-programmed responses) + InMemoryBackend + real ToolRegistry + real AgentLoop. No network calls. Tests verify the full pipeline from user input through LLM response to tool execution and memory recall.

**Tech Stack:** C++17, CMake, Catch2, existing MockProvider pattern from test_agent_loop.cpp

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `tests/test_integration_agent_memory.cpp` | Agent + Memory integration |
| `tests/test_integration_provider_tool.cpp` | Provider + Tool + LoopDetector integration |
| `tests/test_integration_full_pipeline.cpp` | Full pipeline: Provider → AgentLoop → Tool → Memory |
| `tests/test_integration_scoped_memory.cpp` | ScopedMemory + MemoryManager + AgentLoop |

### Modified Files

| File | Change |
|------|--------|
| `tests/CMakeLists.txt` | Add new test files |

---

## Task 1: Agent + Memory Integration

**Files:**
- Create: `tests/test_integration_agent_memory.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write integration tests**

```cpp
// tests/test_integration_agent_memory.cpp
// Integration: AgentLoop + InMemoryBackend + MemoryManager
// Verifies that memory recall results are injected into system prompt
// and that auto-memory stores/recalls across turns.

#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// Mock Provider that returns preset responses
class MockProviderForMemory : public IProvider {
public:
    std::string name() const override { return "mock-memory"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>& messages,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        // Capture messages for inspection
        last_messages_ = messages;
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

    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    std::queue<LLMResponse> responses_;
    std::vector<Message> last_messages_;
};

TEST_CASE("Integration: AgentLoop with MemoryManager prefetches memories", "[integration][agent][memory]") {
    auto backend = std::make_unique<InMemoryBackend>();
    // Pre-store a memory that should be recalled
    backend->store("user prefers dark mode", "preference", 7);

    auto mgr = std::make_unique<MemoryManager>(std::move(backend));

    auto provider = std::make_unique<MockProviderForMemory>();
    LLMResponse resp;
    resp.content = "I'll use dark mode for you.";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, mgr.get(),
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("set my theme");
    REQUIRE(result.ok());
    REQUIRE(output == "I'll use dark mode for you.");

    // Verify system prompt contains memory content
    auto& msgs = provider->last_messages();
    bool found_memory = false;
    for (const auto& m : msgs) {
        if (m.role == Role::System && m.content.find("dark mode") != std::string::npos) {
            found_memory = true;
            break;
        }
    }
    REQUIRE(found_memory);
}

TEST_CASE("Integration: AgentLoop with null memory still works", "[integration][agent][memory]") {
    auto provider = std::make_unique<MockProviderForMemory>();
    LLMResponse resp;
    resp.content = "Hello!";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("Hi");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello!");
}

TEST_CASE("Integration: MemoryManager sync_turn lifecycle", "[integration][memory]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("fact about C++", "core", 7);

    MemoryManager mgr(std::move(backend));

    // Prefetch
    auto results = mgr.prefetch("C++");
    REQUIRE(results.size() >= 1);

    // Sync turn
    mgr.sync_turn("tell me about C++", "C++ is a systems programming language");

    // Next prefetch should still work
    auto results2 = mgr.prefetch("C++");
    REQUIRE(results2.size() >= 1);

    // Build memory block
    auto block = mgr.build_memory_block();
    REQUIRE_FALSE(block.empty());
}
```

- [ ] **Step 2: Add to CMakeLists.txt**

Add `test_integration_agent_memory.cpp` to `ea-tests` source list.

- [ ] **Step 3: Build and run**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[integration]" --reporter compact`
Expected: All 3 tests pass.

---

## Task 2: Provider + Tool + LoopDetector Integration

**Files:**
- Create: `tests/test_integration_provider_tool.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write integration tests**

```cpp
// tests/test_integration_provider_tool.cpp
// Integration: Provider → AgentLoop → ToolRegistry → LoopDetector
// Verifies multi-turn tool calling, loop detection, and error handling.

#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "tool/Toolset.h"
#include "core/ITool.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock Provider with response queue
class IntegrationProvider : public IProvider {
public:
    std::string name() const override { return "integration"; }
    std::vector<std::string> list_models() const override { return {"test-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue(LLMResponse resp) { responses_.push(std::move(resp)); }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>& specs,
                              const std::string& sys,
                              const ChatOptions&) override {
        call_count_++;
        last_specs_ = specs;
        last_messages_ = msgs;
        if (responses_.empty()) return Error::net("no responses");
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int call_count() const { return call_count_; }
    const std::vector<ToolSpec>& last_specs() const { return last_specs_; }
    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    std::queue<LLMResponse> responses_;
    int call_count_ = 0;
    std::vector<ToolSpec> last_specs_;
    std::vector<Message> last_messages_;
};

// Counting tool — tracks how many times it was called
class CountingTool : public ITool {
public:
    explicit CountingTool(std::string n, std::string output = "ok")
        : name_(std::move(n)), output_(std::move(output)), count_(0) {}

    std::string name() const override { return name_; }
    std::string description() const override { return "counting tool"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        count_++;
        return ToolResult{"call_" + std::to_string(count_), output_, false};
    }

    int count() const { return count_; }

private:
    std::string name_;
    std::string output_;
    int count_;
};

// Tool that always errors
class ErrorTool : public ITool {
public:
    std::string name() const override { return "error_tool"; }
    std::string description() const override { return "always fails"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return Error::tool_error("tool execution failed");
    }
};

static ToolCall make_call(const std::string& tool, const std::string& id, const json& args = json::object()) {
    ToolCall tc;
    tc.id = id;
    tc.name = tool;
    tc.arguments = args;
    return tc;
}

static LLMResponse make_text_response(const std::string& text) {
    LLMResponse resp;
    resp.content = text;
    resp.stop_reason = "stop";
    return resp;
}

static LLMResponse make_tool_response(const std::vector<ToolCall>& calls) {
    LLMResponse resp;
    resp.content = "";
    resp.stop_reason = "tool_use";
    resp.tool_calls = calls;
    return resp;
}

TEST_CASE("Integration: multi-turn tool call chain", "[integration][provider][tool]") {
    auto provider = std::make_unique<IntegrationProvider>();

    // Turn 1: LLM calls counter_a
    provider->enqueue(make_tool_response({make_call("counter_a", "c1")}));
    // Turn 2: LLM calls counter_b
    provider->enqueue(make_tool_response({make_call("counter_b", "c2")}));
    // Turn 3: LLM gives final answer
    provider->enqueue(make_text_response("Done after two tool calls"));

    auto tool_a = new CountingTool("counter_a");
    auto tool_b = new CountingTool("counter_b");
    int count_a = 0, count_b = 0;

    ToolRegistry registry;
    registry.register_tool(std::unique_ptr<ITool>(tool_a));
    registry.register_tool(std::unique_ptr<ITool>(tool_b));

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("do the thing");
    REQUIRE(result.ok());
    REQUIRE(output == "Done after two tool calls");
    REQUIRE(tool_a->count() == 1);
    REQUIRE(tool_b->count() == 1);
    REQUIRE(provider->call_count() == 3);
}

TEST_CASE("Integration: tool specs passed to provider", "[integration][provider][tool]") {
    auto provider = std::make_unique<IntegrationProvider>();
    provider->enqueue(make_text_response("ok"));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<CountingTool>("my_tool"));

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    loop.run("test");
    REQUIRE(provider->last_specs().size() == 1);
    REQUIRE(provider->last_specs()[0].name == "my_tool");
}

TEST_CASE("Integration: tool error handled gracefully", "[integration][provider][tool]") {
    auto provider = std::make_unique<IntegrationProvider>();

    // Turn 1: LLM calls error_tool
    provider->enqueue(make_tool_response({make_call("error_tool", "c1")}));
    // Turn 2: LLM sees error and responds
    provider->enqueue(make_text_response("The tool failed, but I can still help."));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<ErrorTool>());

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("try the error tool");
    REQUIRE(result.ok());
    REQUIRE(output == "The tool failed, but I can still help.");
}

TEST_CASE("Integration: toolset activation controls available tools", "[integration][tool]") {
    auto provider = std::make_unique<IntegrationProvider>();
    provider->enqueue(make_text_response("ok"));

    ToolRegistry registry;
    auto set_a = std::make_unique<tool::Toolset>("set_a");
    set_a->add(std::make_unique<CountingTool>("tool_a"));
    set_a->add(std::make_unique<CountingTool>("tool_b"));

    auto set_b = std::make_unique<tool::Toolset>("set_b");
    set_b->add(std::make_unique<CountingTool>("tool_c"));

    registry.register_toolset(std::move(set_a));
    registry.register_toolset(std::move(set_b));

    // Deactivate set_b
    registry.deactivate("set_b");

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    loop.run("test");
    // Only set_a tools should be in specs
    auto& specs = provider->last_specs();
    REQUIRE(specs.size() == 2);
    std::set<std::string> names;
    for (const auto& s : specs) names.insert(s.name);
    REQUIRE(names.count("tool_a") == 1);
    REQUIRE(names.count("tool_b") == 1);
    REQUIRE(names.count("tool_c") == 0);
}

TEST_CASE("Integration: loop detection blocks exact repeat", "[integration][loopdetect]") {
    auto provider = std::make_unique<IntegrationProvider>();

    // LLM keeps calling the same tool with same args
    for (int i = 0; i < 10; ++i) {
        provider->enqueue(make_tool_response({make_call("counter", "c" + std::to_string(i),
                                                         json{{"q", "same"}})}));
    }
    // Final response (may not be reached due to loop detection)
    provider->enqueue(make_text_response("done"));

    auto counter = new CountingTool("counter", "same result");

    ToolRegistry registry;
    registry.register_tool(std::unique_ptr<ITool>(counter));

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("loop test");
    // Loop detection should have kicked in — counter should NOT have been called 10 times
    REQUIRE(counter->count() < 10);
}
```

- [ ] **Step 2: Add to CMakeLists.txt**

Add `test_integration_provider_tool.cpp` to `ea-tests` source list.

- [ ] **Step 3: Build and run**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[integration]" --reporter compact`
Expected: All 5 tests pass.

---

## Task 3: Full Pipeline Integration

**Files:**
- Create: `tests/test_integration_full_pipeline.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write full pipeline tests**

```cpp
// tests/test_integration_full_pipeline.cpp
// End-to-end: Provider → AgentLoop → Tool → Memory → LoopDetector
// Exercises the complete agent stack with mock provider.

#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "memory/ScopedMemory.h"
#include "tool/ToolRegistry.h"
#include "tool/Toolset.h"
#include "core/ITool.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// Shared mock provider
class PipelineProvider : public IProvider {
public:
    std::string name() const override { return "pipeline"; }
    std::vector<std::string> list_models() const override { return {"test"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue(LLMResponse resp) { responses_.push(std::move(resp)); }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>& specs,
                              const std::string&,
                              const ChatOptions&) override {
        last_msg_count_ = msgs.size();
        last_spec_count_ = specs.size();
        if (responses_.empty()) return Error::net("empty");
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    size_t last_msg_count() const { return last_msg_count_; }
    size_t last_spec_count() const { return last_spec_count_; }

private:
    std::queue<LLMResponse> responses_;
    size_t last_msg_count_ = 0;
    size_t last_spec_count_ = 0;
};

// Memory tool that uses the backend directly
class IntegrationMemoryTool : public ITool {
public:
    explicit IntegrationMemoryTool(IMemory* mem) : mem_(mem) {}
    std::string name() const override { return "remember"; }
    std::string description() const override { return "Store or recall memories"; }
    json parameters_schema() const override {
        return json::parse(R"({
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["store", "recall"]},
                "content": {"type": "string"}
            },
            "required": ["action"]
        })");
    }
    bool is_mutating() const override { return false; }
    Result<ToolResult> execute(const json& args) override {
        std::string action = args.value("action", "recall");
        if (action == "store") {
            auto r = mem_->store(args.value("content", ""), "core", 5);
            if (!r.ok()) return r.error();
            return ToolResult{"mem_1", "Stored: " + r.value(), false};
        } else {
            auto r = mem_->recall(args.value("content", ""), 5);
            if (!r.ok()) return r.error();
            std::string result;
            for (const auto& e : r.value()) {
                result += e.content + "\n";
            }
            return ToolResult{"mem_1", result.empty() ? "No memories found" : result, false};
        }
    }
private:
    IMemory* mem_;
};

static ToolCall make_call(const std::string& tool, const std::string& id, const json& args = json::object()) {
    ToolCall tc;
    tc.id = id;
    tc.name = tool;
    tc.arguments = args;
    return tc;
}

static LLMResponse make_text(const std::string& text) {
    LLMResponse r;
    r.content = text;
    r.stop_reason = "stop";
    return r;
}

static LLMResponse make_tools(std::vector<ToolCall> calls) {
    LLMResponse r;
    r.content = "";
    r.stop_reason = "tool_use";
    r.tool_calls = std::move(calls);
    return r;
}

TEST_CASE("Full pipeline: user → LLM → tool → memory → response", "[integration][full]") {
    auto backend = std::make_unique<InMemoryBackend>();
    auto mem_ptr = backend.get();
    MemoryManager mgr(std::move(backend));

    auto provider = std::make_unique<PipelineProvider>();

    // Turn 1: LLM calls remember tool to store
    provider->enqueue(make_tools({make_call("remember", "c1", json{{"action", "store"}, {"content", "user likes Python"}})}));
    // Turn 2: LLM responds
    provider->enqueue(make_text("I'll remember that you like Python!"));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<IntegrationMemoryTool>(mem_ptr));

    std::string output;
    AgentLoop loop(provider.get(), &registry, &mgr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("I like Python");
    REQUIRE(result.ok());
    REQUIRE(output == "I'll remember that you like Python!");

    // Verify memory was actually stored
    auto mem_result = mem_ptr->recall("Python");
    REQUIRE(mem_result.ok());
    REQUIRE(mem_result.value().size() >= 1);
}

TEST_CASE("Full pipeline: multi-tool call in single turn", "[integration][full]") {
    auto provider = std::make_unique<PipelineProvider>();

    // LLM calls two tools at once
    provider->enqueue(make_tools({
        make_call("echo_a", "c1"),
        make_call("echo_b", "c2")
    }));
    provider->enqueue(make_text("Both tools executed"));

    class EchoTool : public ITool {
    public:
        explicit EchoTool(std::string n) : name_(std::move(n)) {}
        std::string name() const override { return name_; }
        std::string description() const override { return "echo"; }
        json parameters_schema() const override { return json::object(); }
        Result<ToolResult> execute(const json&) override {
            return ToolResult{name_ + "_result", "echo from " + name_, false};
        }
    private:
        std::string name_;
    };

    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>("echo_a"));
    registry.register_tool(std::make_unique<EchoTool>("echo_b"));

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("call both");
    REQUIRE(result.ok());
    REQUIRE(output == "Both tools executed");
}

TEST_CASE("Full pipeline: ScopedMemory with agent isolation", "[integration][full]") {
    // Create shared backend
    auto backend = std::make_unique<InMemoryBackend>();

    // Store from agent-A
    MemoryScope scope_a;
    scope_a.agent_id = "agent-A";
    ScopedMemory scoped_a(std::make_unique<InMemoryBackend>(), scope_a);
    scoped_a.store("agent-A secret", "core", 5);

    // Store from agent-B
    MemoryScope scope_b;
    scope_b.agent_id = "agent-B";
    ScopedMemory scoped_b(std::make_unique<InMemoryBackend>(), scope_b);
    scoped_b.store("agent-B secret", "core", 5);

    // Agent-A can read own entries
    auto a_results = scoped_a.recall("secret");
    REQUIRE(a_results.ok());
    REQUIRE(a_results.value().size() == 1);
    REQUIRE(a_results.value()[0].content == "agent-A secret");

    // Agent-B can read own entries
    auto b_results = scoped_b.recall("secret");
    REQUIRE(b_results.ok());
    REQUIRE(b_results.value().size() == 1);
    REQUIRE(b_results.value()[0].content == "agent-B secret");
}

TEST_CASE("Full pipeline: interrupt stops agent mid-loop", "[integration][full]") {
    auto provider = std::make_unique<PipelineProvider>();

    // Always request tool use (infinite loop)
    for (int i = 0; i < 20; ++i) {
        provider->enqueue(make_tools({make_call("echo", "c" + std::to_string(i))}));
    }

    class EchoTool : public ITool {
    public:
        std::string name() const override { return "echo"; }
        std::string description() const override { return "echo"; }
        json parameters_schema() const override { return json::object(); }
        Result<ToolResult> execute(const json&) override {
            return ToolResult{"1", "echo", false};
        }
    };

    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    // Interrupt after a short delay (simulate from another thread)
    std::thread interrupter([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        loop.interrupt();
    });

    auto result = loop.run("loop test");
    interrupter.join();

    // Should have been interrupted (error return)
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Full pipeline: history preserved across runs", "[integration][full]") {
    auto provider = std::make_unique<PipelineProvider>();

    // First run
    provider->enqueue(make_text("First response"));

    // Second run
    provider->enqueue(make_text("Second response"));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto r1 = loop.run("First question");
    REQUIRE(r1.ok());
    REQUIRE(output == "First response");

    auto r2 = loop.run("Second question");
    REQUIRE(r2.ok());
    REQUIRE(output == "Second response");

    // History should contain both exchanges
    auto& history = loop.history();
    REQUIRE(history.size() == 4);  // 2 user + 2 assistant
    REQUIRE(history[0].role == Role::User);
    REQUIRE(history[0].content == "First question");
    REQUIRE(history[2].role == Role::User);
    REQUIRE(history[2].content == "Second question");
}
```

- [ ] **Step 2: Add to CMakeLists.txt**

Add `test_integration_full_pipeline.cpp` to `ea-tests` source list.

- [ ] **Step 3: Build and run**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[integration]" --reporter compact`
Expected: All 5 tests pass.

---

## Task 4: Final Verification

- [ ] **Step 1: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests --reporter compact`
Expected: All tests pass, zero regressions.

- [ ] **Step 2: Verify integration test count**

Run: `./tests/ea-tests "[integration]" --reporter compact`
Expected: 13 integration test cases pass.
