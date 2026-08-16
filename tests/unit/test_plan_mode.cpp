// tests/unit/test_plan_mode.cpp — plan mode filtering, plan_exit and handoff
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "agent/PlanMode.h"
#include "agent/PlanExitTool.h"
#include "agent/steps/ExecuteToolsStep.h"
#include "agent/steps/BuildToolSpecsStep.h"
#include "tool/FileTool.h"
#include "tool/ShellTool.h"
#include "tool/SearchTool.h"
#include "security/IApprovalHandler.h"
#include "conversation/SqliteConversationStore.h"
#include "MockProvider.h"
#include <filesystem>
#include <atomic>
#include <memory>
#include <unistd.h>
#include <vector>

using namespace ea;
using namespace ea::agent;
using namespace ea::tool;
using ea::test::MockProvider;

namespace {

class MockApproval : public security::IApprovalHandler {
public:
    security::ApprovalDecision decision = security::ApprovalDecision::Approved;
    int calls = 0;

    security::ApprovalDecision request_approval(
        const security::ApprovalRequest&) override {
        calls++;
        return decision;
    }
};

std::string unique_plan_dir() {
    auto dir = std::filesystem::temp_directory_path()
        / ("ea-plan-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    return dir.string();
}

}  // namespace

TEST_CASE("PlanExitTool requests approval and switches mode", "[plan_mode]") {
    auto state = std::make_shared<PlanModeState>();
    state->active.store(true);
    MockApproval approval;
    approval.decision = security::ApprovalDecision::Approved;
    PlanExitTool tool(&approval, state);

    auto result = tool.execute(json::object());
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value().is_error);
    REQUIRE_FALSE(state->active.load());
    REQUIRE(state->exit_approved.load());
    REQUIRE(approval.calls == 1);

    state->active.store(true);
    state->exit_approved.store(false);
    approval.decision = security::ApprovalDecision::Rejected;
    result = tool.execute(json::object());
    REQUIRE(result.ok());
    REQUIRE(state->active.load());
    REQUIRE_FALSE(state->exit_approved.load());
}

TEST_CASE("BuildToolSpecsStep hides mutating tools in plan mode", "[plan_mode]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    registry.register_tool(std::make_unique<ShellTool>());
    registry.register_tool(std::make_unique<SearchTool>());

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.plan_mode = true;

    BuildToolSpecsStep step;
    REQUIRE(step.execute(ctx).ok());

    std::vector<std::string> names;
    for (const auto& spec : ctx.tool_specs) names.push_back(spec.name);
    REQUIRE(std::find(names.begin(), names.end(), "file") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "search_files") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "shell") == names.end());
}

TEST_CASE("ExecuteToolsStep enforces plan file write limit", "[plan_mode]") {
    std::string dir = unique_plan_dir();
    std::string plan_file = dir + "/plan.md";
    std::string other_file = dir + "/other.md";

    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    registry.register_tool(std::make_unique<ShellTool>());

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.plan_mode = true;
    ctx.plan_file = plan_file;

    ExecuteToolsStep step(nullptr, nullptr);

    ctx.pending_tool_calls = {
        {"shell", "shell", json{{"command", "touch /tmp/x"}}},
        {"file", "file", json{{"action", "write"}, {"path", other_file}, {"content", "x"}}},
        {"file", "file", json{{"action", "write"}, {"path", plan_file}, {"content", "# Plan"}}},
    };
    REQUIRE(step.execute(ctx).ok());
    REQUIRE(ctx.tool_results.size() == 3);
    REQUIRE(ctx.tool_results[0].is_error);
    REQUIRE(ctx.tool_results[1].is_error);
    REQUIRE_FALSE(ctx.tool_results[2].is_error);
    REQUIRE(std::filesystem::exists(plan_file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("AgentLoop switches plan -> build after plan_exit approval", "[plan_mode]") {
    auto state = std::make_shared<PlanModeState>();
    MockApproval approval;
    approval.decision = security::ApprovalDecision::Approved;

    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    registry.register_tool(std::make_unique<ShellTool>());
    registry.register_tool(std::make_unique<SearchTool>());
    registry.register_tool(std::make_unique<PlanExitTool>(&approval, state));

    auto provider = std::make_shared<MockProvider>();
    provider->enqueue_tool_calls({{"exit", "plan_exit", json::object()}});
    provider->enqueue_text("Implemented the plan");

    AgentLoop::Config cfg;
    cfg.max_iterations = 10;
    cfg.stream = false;
    cfg.auto_memory = false;
    cfg.auto_persist = false;
    cfg.model = "mock-model";
    cfg.plan_mode = state;
    cfg.plan_dir = unique_plan_dir();

    AgentLoop loop(provider.get(), &registry, nullptr, cfg, nullptr, nullptr);
    REQUIRE(loop.set_plan_mode(true).ok());
    REQUIRE(loop.plan_mode());
    REQUIRE(!loop.plan_file().empty());

    auto result = loop.run("Plan a feature");
    REQUIRE(result.ok());
    REQUIRE_FALSE(loop.plan_mode());

    bool saw_handoff = false;
    for (const auto& msg : loop.history()) {
        if (msg.role == Role::User &&
            msg.content.rfind("Plan approved. Execute the plan", 0) == 0) {
            saw_handoff = true;
        }
    }
    REQUIRE(saw_handoff);
    REQUIRE(provider->call_count() >= 2);

    // Build-mode specs after the switch include mutating tools again.
    bool saw_shell = false;
    for (const auto& spec : provider->last_specs()) {
        if (spec.name == "shell") saw_shell = true;
    }
    REQUIRE(saw_shell);
}

TEST_CASE("Conversation store round-trips plan mode fields", "[plan_mode]") {
    std::string db = unique_plan_dir() + "/conv.db";
    conversation::SqliteConversationStore store{{db}};
    REQUIRE(store.open().ok());

    auto id = store.create("m");
    REQUIRE(id.ok());
    Message msg{Role::User, "Plan this"};
    msg.mode = "plan";
    msg.plan_file = "/tmp/plan.md";
    REQUIRE(store.append(id.value(), msg).ok());

    auto loaded = store.load(id.value());
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().size() == 1);
    REQUIRE(loaded.value()[0].mode == "plan");
    REQUIRE(loaded.value()[0].plan_file == "/tmp/plan.md");

    auto exported = store.export_jsonl(id.value());
    REQUIRE(exported.ok());
    auto imported = store.import_jsonl(exported.value());
    REQUIRE(imported.ok());
    auto imported_msgs = store.load(imported.value());
    REQUIRE(imported_msgs.ok());
    REQUIRE(imported_msgs.value()[0].mode == "plan");
    REQUIRE(imported_msgs.value()[0].plan_file == "/tmp/plan.md");

    store.close();
    std::filesystem::remove_all(std::filesystem::temp_directory_path()
        / ("ea-plan-test-" + std::to_string(::getpid())));
}
