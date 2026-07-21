// tests/integration/test_fallback_integration.cpp
// Integration: ReliableProvider falls back on primary failure
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "provider/ReliableProvider.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;
using namespace ea::provider;
using namespace ea::tool;

TEST_CASE("Integration: ReliableProvider falls back on primary failure", "[integration][provider]") {
    // Primary provider always fails
    auto primary = std::make_shared<MockProvider>("failing-primary");
    primary->set_chat_handler([](const std::vector<Message>&,
                                 const std::vector<ToolSpec>&,
                                 const std::string&, const ChatOptions&) -> Result<LLMResponse> {
        return Error::net("primary unavailable");
    });

    // Fallback provider works normally
    auto fallback = std::make_shared<MockProvider>("fallback");
    fallback->enqueue_text("Fallback response");

    ReliableProvider::Config config;
    config.max_retries = 0;  // No retries, go straight to fallback
    config.fallbacks = {fallback};

    ReliableProvider reliable(primary, config);

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(&reliable, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Fallback response");
}
