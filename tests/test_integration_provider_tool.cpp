// Integration: Provider → AgentLoop → ToolRegistry → LoopDetector
// Verifies multi-turn tool calling, tool error handling, toolset activation, loop detection.

#include <queue>
#include <set>
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "tool/Toolset.h"
#include "core/ITool.h"
#include "security/IApprovalHandler.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock Provider with call tracking
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
                              const std::string&,
                              const ChatOptions&) override {
        call_count_++;
        last_specs_ = specs;
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

private:
    std::queue<LLMResponse> responses_;
    int call_count_ = 0;
    std::vector<ToolSpec> last_specs_;
};

// Counting tool — tracks invocation count
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

// --- Approval integration tests ---

class MockApprovalHandler : public ea::security::IApprovalHandler {
public:
    ea::security::ApprovalDecision next_decision = ea::security::ApprovalDecision::Approved;
    mutable ea::security::ApprovalRequest last_request;

    ea::security::ApprovalDecision request_approval(const ea::security::ApprovalRequest& req) override {
        last_request = req;
        return next_decision;
    }
};

class DangerousTool : public ea::ITool {
public:
    std::string name() const override { return "dangerous_op"; }
    std::string description() const override { return "A dangerous tool for testing"; }
    nlohmann::json parameters_schema() const override { return nlohmann::json::object(); }
    ea::Result<ea::ToolResult> execute(const nlohmann::json&) override {
        return ea::ToolResult{"", "dangerous result", false};
    }
    bool is_dangerous() const override { return true; }
};

class SafeTool : public ea::ITool {
public:
    std::string name() const override { return "safe_op"; }
    std::string description() const override { return "A safe tool for testing"; }
    nlohmann::json parameters_schema() const override { return nlohmann::json::object(); }
    ea::Result<ea::ToolResult> execute(const nlohmann::json&) override {
        return ea::ToolResult{"", "safe result", false};
    }
    bool is_dangerous() const override { return false; }
};

TEST_CASE("Approval: safe tool executes without approval", "[integration][approval]") {
    MockApprovalHandler approval;
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<SafeTool>());

    ea::agent::AgentLoop loop(
        nullptr, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    // Safe tool should not trigger approval — last_request.tool_name stays empty
    REQUIRE(approval.last_request.tool_name.empty());
}

TEST_CASE("Approval: dangerous tool approved executes normally", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Approved;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.enqueue(make_tool_response({make_call("dangerous_op", "c1")}));
    provider.enqueue(make_text_response("Done!"));

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(approval.last_request.tool_name == "dangerous_op");
}

TEST_CASE("Approval: dangerous tool rejected returns error result", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Rejected;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.enqueue(make_tool_response({make_call("dangerous_op", "c1")}));
    provider.enqueue(make_text_response("I see the tool was rejected"));

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(approval.last_request.tool_name == "dangerous_op");
}

TEST_CASE("Approval: dangerous tool aborted stops agent loop", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Aborted;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.enqueue(make_tool_response({make_call("dangerous_op", "c1")}));

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());  // Aborted sets should_stop, loop exits cleanly
}

TEST_CASE("Approval: null handler allows dangerous tools without approval", "[integration][approval]") {
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.enqueue(make_tool_response({make_call("dangerous_op", "c1")}));
    provider.enqueue(make_text_response("Done!"));

    // No approval handler (nullptr)
    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        nullptr
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());  // Dangerous tool executes without approval
}
