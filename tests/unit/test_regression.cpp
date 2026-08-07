// tests/unit/test_regression.cpp
// Regression tests — validate fixes for historical bugs
#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include "memory/HolographicMemory.h"
#include "memory/ScopedMemory.h"
#include "base/Error.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;
using namespace ea::memory;
using namespace ea::tool;

TEST_CASE("Regression: AgentLoop handles empty tool_calls with tool_use stop_reason",
          "[regression][agent]") {
    // Issue: LLM returns stop_reason="tool_use" but tool_calls is empty
    // Previously caused crash or infinite loop
    MockProvider provider;
    LLMResponse resp;
    resp.content = "";
    resp.stop_reason = "tool_use";
    resp.tool_calls = {};  // empty!
    provider.enqueue(std::move(resp));
    provider.enqueue_text("Recovered from empty tool calls");

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(&provider, &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());  // should not crash
}

TEST_CASE("Regression: ScopedMemory empty read_allowlist allows own scope",
          "[regression][memory]") {
    // Issue: empty read_allowlist should allow reading own scope's memories
    MemoryScope scope;
    scope.agent_id = "test-agent";
    // read_allowlist is empty
    auto backend = std::make_unique<HolographicMemory>(HolographicMemoryConfig{":memory:", false});
    backend->open();
    ScopedMemory scoped(std::move(backend), scope);
    scoped.store("my fact", "core", 5);

    auto results = scoped.recall("fact", 5);
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "my fact");
}

TEST_CASE("Regression: Error struct all fields initialized",
          "[regression][types]") {
    // Issue: Error aggregate requires all fields initialized
    Error e1 = Error::net("test");
    REQUIRE(e1.code == ErrorCode::NetworkError);
    REQUIRE(e1.message == "test");
    REQUIRE(e1.http_status == 0);
    REQUIRE(e1.detail.empty());

    Error e2{ErrorCode::Unknown, "msg", 0, {}};
    REQUIRE(e2.code == ErrorCode::Unknown);
}
