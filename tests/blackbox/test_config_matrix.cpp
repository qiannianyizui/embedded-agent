// tests/blackbox/test_config_matrix.cpp
// Configuration matrix tests — verify AgentLoop and components behave correctly
// across different configuration combinations.
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "memory/HolographicMemory.h"
#include "provider/ReliableProvider.h"
#include "budget/BudgetTracker.h"
#include "budget/Types.h"
#include "nlohmann/json.hpp"

using namespace ea;
using namespace ea::agent;
using namespace ea::test;
using namespace ea::security;
using namespace ea::provider;
using namespace ea::budget;
using json = nlohmann::json;

// ── 1. AgentLoop with max_iterations=1 stops after one turn ────────────────────

TEST_CASE("Config matrix: max_iterations=1 stops after one turn", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    // Return a tool call — would normally loop, but max_iterations=1 stops it
    ToolCall tc;
    tc.id = "call_1";
    tc.name = "test_tool";
    tc.arguments = json::object();
    mock->enqueue_tool_calls({tc});

    MockTool tool("test_tool", "result");
    ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>(std::move(tool)));

    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    AgentLoop::Config cfg;
    cfg.max_iterations = 1;
    AgentLoop loop(mock.get(), &registry, nullptr, cfg,
                   [&](const std::string& t) { output = t; }, nullptr, &policy);

    auto result = loop.run("test single iteration");
    REQUIRE(result.ok());
    // Provider should have been called exactly once
    REQUIRE(mock->call_count() == 1);
}

// ── 2. AgentLoop without memory still works ────────────────────────────────────

TEST_CASE("Config matrix: AgentLoop without memory still works", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_text("Hello without memory");

    ToolRegistry registry;
    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    AgentLoop loop(mock.get(), &registry, nullptr, AgentLoop::Config{},
                   [&](const std::string& t) { output = t; }, nullptr, &policy);

    auto result = loop.run("test no memory");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello without memory");
}

// ── 3. AgentLoop without tools still works ─────────────────────────────────────

TEST_CASE("Config matrix: AgentLoop without tools still works", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_text("Hello without tools");

    ToolRegistry registry;  // empty registry
    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    AgentLoop loop(mock.get(), &registry, nullptr, AgentLoop::Config{},
                   [&](const std::string& t) { output = t; }, nullptr, &policy);

    auto result = loop.run("test no tools");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello without tools");
}

// ── 4. SecurityPolicy ReadOnly blocks all mutating tools ───────────────────────

TEST_CASE("Config matrix: ReadOnly blocks all mutating tools", "[config-matrix][greybox]") {
    SecurityPolicy policy(AutonomyLevel::ReadOnly);

    // ReadOnly should block tools not in the READONLY_TOOLS list
    auto result = policy.check_tool("shell");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);

    // ReadOnly should block file tool
    auto file_result = policy.check_tool("file");
    REQUIRE_FALSE(file_result.ok());

    // ReadOnly should allow search_files (in READONLY_TOOLS list)
    auto search_result = policy.check_tool("search_files");
    REQUIRE(search_result.ok());

    // ReadOnly should allow web (in READONLY_TOOLS list)
    auto web_result = policy.check_tool("web");
    REQUIRE(web_result.ok());
}

// ── 5. SecurityPolicy Full allows all tools ────────────────────────────────────

TEST_CASE("Config matrix: Full allows all tools", "[config-matrix][greybox]") {
    SecurityPolicy policy(AutonomyLevel::Full);

    REQUIRE(policy.check_tool("shell").ok());
    REQUIRE(policy.check_tool("file").ok());
    REQUIRE(policy.check_tool("search_files").ok());
    REQUIRE(policy.check_tool("any_tool").ok());
}

// ── 6. SecurityPolicy Supervised requires approval for dangerous ───────────────

TEST_CASE("Config matrix: Supervised allows non-dangerous tools", "[config-matrix][greybox]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);

    // Supervised allows all tools (approval is handled by IApprovalHandler, not check_tool)
    REQUIRE(policy.check_tool("shell").ok());
    REQUIRE(policy.check_tool("search_files").ok());
}

TEST_CASE("Config matrix: Supervised blocks dangerous commands", "[config-matrix][greybox]") {
    SecurityPolicy policy(AutonomyLevel::Supervised);

    // Dangerous commands are always blocked unless Full
    auto result = policy.check_command("rm -rf /");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::SecurityBlocked);
}

// ── 7. AgentLoop with streaming enabled ────────────────────────────────────────

TEST_CASE("Config matrix: AgentLoop with streaming enabled", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    // When stream=true, AgentLoop uses stream_chat, so we need to enqueue chunks
    StreamChunk content_chunk;
    content_chunk.type = StreamChunk::Type::Content;
    content_chunk.data = "streamed response";
    StreamChunk done_chunk;
    done_chunk.type = StreamChunk::Type::Done;
    mock->enqueue_chunks({content_chunk, done_chunk});

    ToolRegistry registry;
    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    std::string stream_data;
    AgentLoop::Config cfg;
    cfg.stream = true;
    AgentLoop loop(mock.get(), &registry, nullptr, cfg,
                   [&](const std::string& t) { output = t; },
                   [&](const StreamChunk& chunk) {
                       if (chunk.type == StreamChunk::Type::Content) {
                           stream_data += chunk.data;
                       }
                   },
                   &policy);

    auto result = loop.run("test streaming enabled");
    REQUIRE(result.ok());
    REQUIRE(stream_data == "streamed response");
}

// ── 8. AgentLoop with streaming disabled ───────────────────────────────────────

TEST_CASE("Config matrix: AgentLoop with streaming disabled", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_text("non-streamed response");

    ToolRegistry registry;
    SecurityPolicy policy(AutonomyLevel::Full);
    std::string output;
    AgentLoop::Config cfg;
    cfg.stream = false;
    AgentLoop loop(mock.get(), &registry, nullptr, cfg,
                   [&](const std::string& t) { output = t; }, nullptr, &policy);

    auto result = loop.run("test streaming disabled");
    REQUIRE(result.ok());
    REQUIRE(output == "non-streamed response");
}

// ── 9. ReliableProvider with 0 retries ─────────────────────────────────────────

TEST_CASE("Config matrix: ReliableProvider with 0 retries fails immediately", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_error(Error::net("connection failed"));

    ReliableProvider::Config cfg;
    cfg.max_retries = 0;
    ReliableProvider reliable(mock, cfg);

    auto result = reliable.chat({}, {}, "model");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::NetworkError);
    // With 0 retries, provider should be called exactly once
    REQUIRE(mock->call_count() == 1);
}

// ── 10. BudgetTracker with warn limit ──────────────────────────────────────────

TEST_CASE("Config matrix: BudgetTracker with warn limit", "[config-matrix][greybox]") {
    auto mock = std::make_shared<MockProvider>();
    LLMResponse resp;
    resp.content = "response";
    resp.stop_reason = "stop";
    resp.usage.input_tokens = 50;
    resp.usage.output_tokens = 20;
    mock->enqueue(std::move(resp));

    BudgetConfig budget_cfg;
    budget_cfg.warn_input_tokens = 100;   // Warn after 100 input tokens
    budget_cfg.warn_output_tokens = 50;   // Warn after 50 output tokens
    budget_cfg.warn_cost_usd = 1.0;

    BudgetTracker tracker(mock, budget_cfg);

    auto result = tracker.chat({}, {}, "model");
    REQUIRE(result.ok());

    // After one call with 50 input tokens, should not be over warn yet
    REQUIRE_FALSE(tracker.is_over_warn());

    // Session usage should reflect the call
    auto usage = tracker.session_usage();
    REQUIRE(usage.input_tokens == 50);
    REQUIRE(usage.output_tokens == 20);
}
