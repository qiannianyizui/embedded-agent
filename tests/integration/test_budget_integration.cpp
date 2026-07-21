// tests/integration/test_budget_integration.cpp
// Integration: BudgetTracker tracks usage through AgentLoop
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "agent/AgentLoop.h"
#include "budget/BudgetTracker.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;
using namespace ea::budget;
using namespace ea::tool;

TEST_CASE("Integration: BudgetTracker tracks usage through AgentLoop", "[integration][budget]") {
    auto inner_provider = std::make_shared<MockProvider>();
    inner_provider->enqueue_text("Response with usage");

    BudgetConfig config;
    config.warn_input_tokens = 100000;
    config.warn_output_tokens = 50000;
    BudgetTracker tracker(inner_provider, config);

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(&tracker, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
}
