// Full pipeline integration: Provider → AgentLoop → Tool → Memory → LoopDetector
// Exercises the complete agent stack with mock provider.

#include <queue>
#include <set>
#include <thread>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "memory/InMemoryBackend.h"
#include "memory/ScopedMemory.h"
#include "tool/ToolRegistry.h"
#include "core/ITool.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// Pipeline mock provider
class PipelineProvider : public IProvider {
public:
    std::string name() const override { return "pipeline"; }
    std::vector<std::string> list_models() const override { return {"test"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue(LLMResponse resp) { responses_.push(std::move(resp)); }

    Result<LLMResponse> chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
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

private:
    std::queue<LLMResponse> responses_;
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

    auto provider = std::make_unique<PipelineProvider>();

    // Turn 1: LLM calls remember tool to store
    provider->enqueue(make_tools({make_call("remember", "c1", json{{"action", "store"}, {"content", "user likes Python"}})}));
    // Turn 2: LLM responds
    provider->enqueue(make_text("I'll remember that you like Python!"));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<IntegrationMemoryTool>(mem_ptr));

    std::string output;
    AgentLoop loop(provider.get(), &registry, mem_ptr,
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
    // Each agent gets its own ScopedMemory wrapping its own InMemoryBackend
    MemoryScope scope_a;
    scope_a.agent_id = "agent-A";
    ScopedMemory scoped_a(std::make_unique<InMemoryBackend>(), scope_a);
    scoped_a.store("agent-A secret", "core", 5);

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

    // Each call has unique args to avoid loop detection
    for (int i = 0; i < 500; ++i) {
        provider->enqueue(make_tools({make_call("echo", "c" + std::to_string(i),
                                                  json{{"n", i}})}));
    }

    class SlowEchoTool : public ITool {
    public:
        std::string name() const override { return "echo"; }
        std::string description() const override { return "echo"; }
        json parameters_schema() const override { return json::object(); }
        Result<ToolResult> execute(const json& args) override {
            // Simulate slow tool execution to give interrupt time to fire
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            int n = args.value("n", 0);
            return ToolResult{"1", "echo " + std::to_string(n), false};
        }
    };

    ToolRegistry registry;
    registry.register_tool(std::make_unique<SlowEchoTool>());

    std::string output;
    AgentLoop::Config cfg;
    cfg.max_iterations = 500;
    AgentLoop loop(provider.get(), &registry, nullptr, cfg,
                   [&](const std::string& t) { output = t; });

    // Interrupt from another thread after 100ms
    std::thread interrupter([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
