// tests/test_subagent.cpp — Subagent + Orchestrator + DelegateTool tests
#include <catch2/catch_test_macros.hpp>
#include "agent/SubagentConfig.h"
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
#include "core/IProvider.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock provider for subagent testing
class MockSubProvider : public IProvider {
public:
    std::string name() const override { return "mock-sub"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void set_next_response(std::string text) {
        next_text_ = std::move(text);
    }

    Result<LLMResponse> chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        call_count_++;
        LLMResponse resp;
        resp.content = next_text_;
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int call_count() const { return call_count_; }

private:
    std::string next_text_;
    int call_count_ = 0;
};

TEST_CASE("SubagentConfig default values", "[subagent]") {
    SubagentConfig config;
    REQUIRE(config.shared_memory == true);
    REQUIRE(config.max_iterations == 20);
    REQUIRE(config.dangerous == false);
    REQUIRE(config.model.empty());
    REQUIRE(config.toolsets.empty());
}

TEST_CASE("SubagentOrchestrator registers and lists templates", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "researcher";
    cfg.description = "Research assistant";
    orchestrator.register_template(cfg);

    REQUIRE(orchestrator.has_template("researcher"));
    REQUIRE_FALSE(orchestrator.has_template("coder"));

    auto templates = orchestrator.available_templates();
    REQUIRE(templates.size() == 1);
    REQUIRE(templates[0] == "researcher");
}

TEST_CASE("SubagentOrchestrator create returns error for unknown template", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    auto result = orchestrator.create("nonexistent");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SubagentOrchestrator delegate returns error for unknown template", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    auto result = orchestrator.delegate("nonexistent", "do something");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SubagentOrchestrator delegate executes subtask", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Research result: found 3 relevant papers");

    tool::ToolRegistry registry;

    SubagentOrchestrator orchestrator(provider.get(), &registry, nullptr);

    SubagentConfig cfg;
    cfg.name = "researcher";
    cfg.description = "Research assistant";
    cfg.system_prompt = "You are a research assistant.";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    auto result = orchestrator.delegate("researcher", "Find papers about AI safety");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "Research result: found 3 relevant papers");
    REQUIRE(provider->call_count() >= 1);
}

TEST_CASE("DelegateTool single delegation", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Task completed successfully");

    tool::ToolRegistry registry;

    SubagentOrchestrator orchestrator(provider.get(), &registry, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    cfg.description = "Worker sub-agent";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    DelegateTool tool(&orchestrator);

    REQUIRE(tool.name() == "delegate");
    REQUIRE(tool.is_dangerous() == true);
    REQUIRE(tool.is_mutating() == true);

    nlohmann::json args = {
        {"agent", "worker"},
        {"task", "Do the thing"}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    REQUIRE(result.value().output == "Task completed successfully");
}

TEST_CASE("DelegateTool returns error for unknown agent", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    DelegateTool tool(&orchestrator);

    nlohmann::json args = {
        {"agent", "nonexistent"},
        {"task", "Do something"}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == true);
}

TEST_CASE("DelegateTool parallel delegation", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Parallel result");

    tool::ToolRegistry registry;

    SubagentOrchestrator orchestrator(provider.get(), &registry, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    cfg.description = "Worker";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    DelegateTool tool(&orchestrator);

    nlohmann::json args = {
        {"agent", "worker"},
        {"task", "placeholder"},
        {"parallel", true},
        {"tasks", nlohmann::json::array({
            {{"agent", "worker"}, {"task", "Task A"}},
            {{"agent", "worker"}, {"task", "Task B"}}
        })}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    // Output should contain results from both tasks
    REQUIRE(result.value().output.find("worker") != std::string::npos);
}

TEST_CASE("SubagentOrchestrator create generates unique IDs", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    orchestrator.register_template(cfg);

    auto s1 = orchestrator.create("worker");
    auto s2 = orchestrator.create("worker");

    REQUIRE(s1.ok());
    REQUIRE(s2.ok());
    REQUIRE(s1.value()->id() != s2.value()->id());
}
