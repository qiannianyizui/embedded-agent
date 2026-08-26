// tests/unit/test_plan_mode.cpp — permission modes: plan filtering, per-mode
// approval policy, plan_exit handoff and mode restoration
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "agent/PermissionMode.h"
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

TEST_CASE("PlanExitTool requests approval and switches to previous mode", "[plan_mode]") {
    auto state = std::make_shared<PermissionState>();
    state->mode.store(PermissionMode::Plan);
    state->previous.store(PermissionMode::Manual);
    MockApproval approval;
    approval.decision = security::ApprovalDecision::Approved;
    PlanExitTool tool(&approval, state);

    auto result = tool.execute(json::object());
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.value().is_error);
    REQUIRE(state->mode.load() == PermissionMode::Manual);
    REQUIRE(state->exit_approved.load());
    REQUIRE(approval.calls == 1);

    state->mode.store(PermissionMode::Plan);
    state->exit_approved.store(false);
    approval.decision = security::ApprovalDecision::Rejected;
    result = tool.execute(json::object());
    REQUIRE(result.ok());
    REQUIRE(state->mode.load() == PermissionMode::Plan);
    REQUIRE_FALSE(state->exit_approved.load());
}

TEST_CASE("PlanExitTool rejects when not in plan mode", "[plan_mode]") {
    auto state = std::make_shared<PermissionState>();
    MockApproval approval;
    PlanExitTool tool(&approval, state);

    auto result = tool.execute(json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error);
    REQUIRE(approval.calls == 0);
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
    ctx.permission_mode = PermissionMode::Plan;

    BuildToolSpecsStep step;
    REQUIRE(step.execute(ctx).ok());

    std::vector<std::string> names;
    for (const auto& spec : ctx.tool_specs) names.push_back(spec.name);
    REQUIRE(std::find(names.begin(), names.end(), "file") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "search_files") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "shell") == names.end());
}

TEST_CASE("BuildToolSpecsStep hides plan_exit outside plan mode", "[plan_mode]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    auto state = std::make_shared<PermissionState>();
    registry.register_tool(std::make_unique<PlanExitTool>(nullptr, state));

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.permission_mode = PermissionMode::AcceptEdits;

    BuildToolSpecsStep step;
    REQUIRE(step.execute(ctx).ok());

    std::vector<std::string> names;
    for (const auto& spec : ctx.tool_specs) names.push_back(spec.name);
    REQUIRE(std::find(names.begin(), names.end(), "plan_exit") == names.end());
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
    ctx.permission_mode = PermissionMode::Plan;
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

TEST_CASE("Default mode asks for every mutating tool", "[plan_mode]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    registry.register_tool(std::make_unique<ShellTool>());
    registry.register_tool(std::make_unique<SearchTool>());

    MockApproval approval;
    approval.decision = security::ApprovalDecision::Approved;

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.permission_mode = PermissionMode::Manual;

    ExecuteToolsStep step(nullptr, &approval);

    ctx.pending_tool_calls = {
        {"shell", "shell", json{{"command", "echo hi"}}},
        {"file", "file", json{{"action", "read"}, {"path", "/tmp/x"}}},
        {"search_files", "search_files", json{{"pattern", "foo"}}},
    };
    REQUIRE(step.execute(ctx).ok());
    REQUIRE(approval.calls == 1);  // only shell asked; reads run free
    REQUIRE_FALSE(ctx.tool_results[0].is_error);
}

TEST_CASE("AcceptEdits mode auto-accepts file edits, asks for the rest", "[plan_mode]") {
    std::string dir = unique_plan_dir();
    std::string target = dir + "/target.md";

    ToolRegistry registry;
    registry.register_tool(std::make_unique<FileTool>());
    registry.register_tool(std::make_unique<ShellTool>());

    MockApproval approval;
    approval.decision = security::ApprovalDecision::Approved;

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.permission_mode = PermissionMode::AcceptEdits;

    ExecuteToolsStep step(nullptr, &approval);

    ctx.pending_tool_calls = {
        {"file", "file", json{{"action", "write"}, {"path", target}, {"content", "x"}}},
        {"shell", "shell", json{{"command", "echo hi"}}},
    };
    REQUIRE(step.execute(ctx).ok());
    REQUIRE(approval.calls == 1);  // file write auto-accepted, shell asked
    REQUIRE_FALSE(ctx.tool_results[0].is_error);
    REQUIRE(std::filesystem::exists(target));

    std::filesystem::remove_all(dir);
}

TEST_CASE("BypassPermissions mode never asks", "[plan_mode]") {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<ShellTool>());

    MockApproval approval;

    std::vector<Message> history;
    std::atomic<bool> interrupted{false};
    TurnContext ctx(history, interrupted);
    ctx.registry = &registry;
    ctx.permission_mode = PermissionMode::BypassPermissions;

    ExecuteToolsStep step(nullptr, &approval);

    ctx.pending_tool_calls = {
        {"shell", "shell", json{{"command", "echo hi"}}},
    };
    REQUIRE(step.execute(ctx).ok());
    REQUIRE(approval.calls == 0);
    REQUIRE_FALSE(ctx.tool_results[0].is_error);
}

TEST_CASE("AgentLoop switches plan -> previous mode after plan_exit approval", "[plan_mode]") {
    auto state = std::make_shared<PermissionState>();
    state->previous.store(PermissionMode::AcceptEdits);
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
    cfg.permission = state;
    cfg.plan_dir = unique_plan_dir();

    AgentLoop loop(provider.get(), &registry, nullptr, cfg, nullptr, nullptr);
    REQUIRE(loop.set_plan_mode(true).ok());
    REQUIRE(loop.plan_mode());
    REQUIRE(!loop.plan_file().empty());

    auto result = loop.run("Plan a feature");
    REQUIRE(result.ok());
    REQUIRE_FALSE(loop.plan_mode());
    REQUIRE(loop.permission_mode() == PermissionMode::AcceptEdits);

    bool saw_handoff = false;
    for (const auto& msg : loop.history()) {
        if (msg.role == Role::User &&
            msg.content.rfind("Plan approved. Execute the plan", 0) == 0) {
            saw_handoff = true;
        }
    }
    REQUIRE(saw_handoff);
    REQUIRE(provider->call_count() >= 2);

    // Tool specs after the switch include mutating tools again.
    bool saw_shell = false;
    for (const auto& spec : provider->last_specs()) {
        if (spec.name == "shell") saw_shell = true;
    }
    REQUIRE(saw_shell);
}

TEST_CASE("set_permission_mode round-trips through plan", "[plan_mode]") {
    auto state = std::make_shared<PermissionState>();
    MockApproval approval;

    AgentLoop::Config cfg;
    cfg.max_iterations = 5;
    cfg.stream = false;
    cfg.auto_memory = false;
    cfg.auto_persist = false;
    cfg.model = "mock-model";
    cfg.permission = state;
    cfg.plan_dir = unique_plan_dir();

    ToolRegistry registry;
    registry.register_tool(std::make_unique<PlanExitTool>(nullptr, state));

    AgentLoop loop(nullptr, &registry, nullptr, cfg, nullptr, nullptr);
    REQUIRE(loop.set_permission_mode(PermissionMode::AcceptEdits).ok());
    REQUIRE(loop.permission_mode() == PermissionMode::AcceptEdits);

    REQUIRE(loop.set_permission_mode(PermissionMode::Plan).ok());
    REQUIRE(loop.plan_mode());

    // Leaving plan mode restores the last non-plan mode.
    REQUIRE(loop.set_permission_mode(PermissionMode::Manual).ok());
    REQUIRE(loop.permission_mode() == PermissionMode::Manual);

    REQUIRE(loop.set_permission_mode(PermissionMode::BypassPermissions).ok());
    REQUIRE(loop.permission_mode() == PermissionMode::BypassPermissions);
    REQUIRE(loop.set_plan_mode(true).ok());
    REQUIRE(loop.plan_mode());
    REQUIRE(loop.set_plan_mode(false).ok());
    REQUIRE(loop.permission_mode() == PermissionMode::BypassPermissions);
}

TEST_CASE("Permission mode names round-trip with legacy aliases", "[plan_mode]") {
    REQUIRE(std::string(permission_mode_name(PermissionMode::Manual)) == "manual");
    REQUIRE(std::string(permission_mode_name(PermissionMode::AcceptEdits))
            == "acceptEdits");
    REQUIRE(std::string(permission_mode_name(PermissionMode::Plan)) == "plan");
    REQUIRE(std::string(permission_mode_name(PermissionMode::BypassPermissions))
            == "auto");

    REQUIRE(permission_mode_from_name("manual") == PermissionMode::Manual);
    REQUIRE(permission_mode_from_name("default") == PermissionMode::Manual);
    REQUIRE(permission_mode_from_name("acceptEdits") == PermissionMode::AcceptEdits);
    REQUIRE(permission_mode_from_name("plan") == PermissionMode::Plan);
    REQUIRE(permission_mode_from_name("auto") == PermissionMode::BypassPermissions);
    REQUIRE(permission_mode_from_name("bypassPermissions")
            == PermissionMode::BypassPermissions);
}

TEST_CASE("Conversation store round-trips permission mode fields", "[plan_mode]") {
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