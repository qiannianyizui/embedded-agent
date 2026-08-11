#include <queue>
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "provider/IProvider.h"
#include "tool/ToolRegistry.h"
#include "tool/ITool.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock Provider that returns preset responses
class MockProvider : public IProvider {
public:
    std::string name() const override { return "mock"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>& messages,
                             const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        if (responses_.empty()) return Error::net("no mock responses");
        last_messages_ = messages;
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented in mock");
    }

private:
    std::queue<LLMResponse> responses_;
public:
    std::vector<Message> last_messages_;
};

// Mock Tool
class MockTool : public ITool {
public:
    MockTool(std::string n, std::string output)
        : name_(std::move(n)), output_(std::move(output)) {}
    std::string name() const override { return name_; }
    std::string description() const override { return "mock"; }
    json parameters_schema() const override { return json::object(); }
    Result<ToolResult> execute(const json&) override {
        return ToolResult{"call_1", output_, false};
    }
private:
    std::string name_;
    std::string output_;
};

TEST_CASE("AgentLoop simple conversation", "[agent]") {
    auto provider = std::make_unique<MockProvider>();
    LLMResponse resp;
    resp.content = "Hello! How can I help?";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr, AgentLoop::Config{}, [&](const std::string& t) { output = t; });

    auto result = loop.run("Hi");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello! How can I help?");
}

TEST_CASE("AgentLoop tool call flow", "[agent]") {
    auto provider = std::make_unique<MockProvider>();

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
    resp2.content = "The answer is 42";
    resp2.stop_reason = "stop";
    provider->enqueue_response(std::move(resp2));

    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("mock_tool", "tool result"));

    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr, AgentLoop::Config{}, [&](const std::string& t) { output = t; });

    auto result = loop.run("What is the answer?");
    REQUIRE(result.ok());
    REQUIRE(output == "The answer is 42");
}

TEST_CASE("AgentLoop max iterations", "[agent]") {
    auto provider = std::make_unique<MockProvider>();

    // Always request tool use (infinite loop)
    for (int i = 0; i < 5; ++i) {
        LLMResponse resp;
        resp.content = "";
        resp.stop_reason = "tool_use";
        ToolCall tc;
        tc.id = "call_" + std::to_string(i);
        tc.name = "mock_tool";
        tc.arguments = json::object();
        resp.tool_calls.push_back(tc);
        provider->enqueue_response(std::move(resp));
    }

    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("mock_tool", "result"));

    std::string output;
    AgentLoop::Config cfg;
    cfg.max_iterations = 3;
    AgentLoop loop(provider.get(), &registry, nullptr, cfg, [&](const std::string& t) { output = t; });

    auto result = loop.run("loop test");
    // Should still return ok (graceful handling)
    REQUIRE(result.ok());
}

TEST_CASE("AgentLoop injects onboarding directive only on first message", "[agent]") {
    auto provider = std::make_unique<MockProvider>();

    LLMResponse resp1;
    resp1.content = "Hello!";
    resp1.stop_reason = "stop";
    provider->enqueue_response(std::move(resp1));

    LLMResponse resp2;
    resp2.content = "Hi again!";
    resp2.stop_reason = "stop";
    provider->enqueue_response(std::move(resp2));

    ToolRegistry registry;
    AgentLoop::Config cfg;
    cfg.onboarding_directive = "\n\n[System note: offer to build a profile]";
    AgentLoop loop(provider.get(), &registry, nullptr, cfg, [](const std::string&) {});

    auto first = loop.run("Hi");
    REQUIRE(first.ok());
    REQUIRE(loop.history()[0].content == "Hi");
    bool saw_directive = false;
    for (const auto& m : provider->last_messages_) {
        if (m.content.find("offer to build a profile") != std::string::npos) {
            saw_directive = true;
        }
    }
    REQUIRE(saw_directive);

    auto second = loop.run("Again");
    REQUIRE(second.ok());
    REQUIRE(loop.history()[2].content == "Again");
    for (const auto& m : provider->last_messages_) {
        REQUIRE(m.content.find("offer to build a profile") == std::string::npos);
    }
}

TEST_CASE("AgentLoop strips legacy onboarding directive on restore", "[agent]") {
    ToolRegistry registry;
    AgentLoop loop(nullptr, &registry, nullptr, AgentLoop::Config{},
                   [](const std::string&) {});

    std::vector<Message> msgs;
    msgs.push_back({Role::User,
                    "你好\n\n[System note: This is the user's very first message ever. "
                    "Offer to build a profile]",
                    std::nullopt, std::nullopt, std::nullopt});

    loop.restore_conversation("conv_test", std::move(msgs));
    REQUIRE(loop.history().size() == 1);
    REQUIRE(loop.history()[0].content == "你好");
}
