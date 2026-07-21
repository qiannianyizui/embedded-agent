// tests/test_events.cpp — Event system integration tests
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "agent/AgentEvent.h"
#include "agent/IEventListener.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/ITool.h"
#include <queue>
#include <thread>
#include <chrono>

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

// Provider that blocks in chat() so interrupt can fire
class MockBlockingProvider : public IProvider {
public:
    std::string name() const override { return "mock-blocking"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, false, false, false, false};
    }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        // Block until interrupted or timeout
        for (int i = 0; i < 100; ++i) {
            if (interrupted_) return Error::net("interrupted");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return Error::net("timeout");
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }
    void notify_interrupt() { interrupted_ = true; }
private:
    std::atomic<bool> interrupted_{false};
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
    // Enqueue a response so the first iteration completes successfully
    LLMResponse resp;
    resp.content = "Working";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    auto listener = std::make_shared<MockEventListener>();
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{10}, [](const std::string&) {});
    loop.add_listener(listener);

    // Interrupt from another thread — the first iteration will complete,
    // but since should_stop is true the loop returns normally.
    // We need the loop to continue to a second iteration where interrupted_ is checked.
    // So enqueue a tool_use response that prevents should_stop, then interrupt.
    // Actually, let's use a simpler approach: interrupt before run() starts,
    // but since run() resets interrupted_, we need to set it during run().
    // The simplest way: use a provider that delays, then interrupt during the delay.
    // But the current MockEventProvider returns immediately.
    // Alternative: set interrupt after first iteration completes but before second starts.
    // Since the loop is single-threaded and fast, we need a different strategy.

    // Use a custom step that pauses to allow interrupt to fire
    class PauseStep : public ITurnStep {
    public:
        Result<void> execute(TurnContext& ctx) override {
            // Small delay to allow interrupt to be set
            for (int i = 0; i < 50; ++i) {
                if (ctx.interrupted) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return {};
        }
        std::string name() const override { return "pause"; }
    };

    // Rebuild with a provider that returns tool_use (so loop continues)
    auto provider2 = std::make_shared<MockEventProvider>();
    LLMResponse resp1;
    resp1.content = "";
    resp1.stop_reason = "tool_use";
    ToolCall tc;
    tc.id = "call_1";
    tc.name = "mock_tool";
    tc.arguments = json::object();
    resp1.tool_calls.push_back(tc);
    provider2->enqueue_response(std::move(resp1));

    registry.register_tool(std::make_unique<MockEventTool>("mock_tool", "result"));

    auto listener2 = std::make_shared<MockEventListener>();
    AgentLoop loop2(provider2.get(), &registry, nullptr,
                    AgentLoop::Config{10}, [](const std::string&) {});
    loop2.add_listener(listener2);
    // Add pause step at the beginning so interrupt can fire
    loop2.add_step(std::make_unique<PauseStep>());

    std::thread interrupter([&loop2]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        loop2.interrupt();
    });

    auto result = loop2.run("test");
    interrupter.join();

    REQUIRE_FALSE(result.ok());
    REQUIRE(listener2->count_of(AgentEventType::Interrupt) >= 1);
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
