// tests/bench/bench_agent_loop.cpp
// Performance benchmarks for AgentLoop single turn and tool call round-trip
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "MockProvider.h"
#include "MockTool.h"
#include "TestHelpers.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;

TEST_CASE("Benchmark: AgentLoop single turn", "[benchmark][agent]") {
    BENCHMARK("single_turn_mock") {
        MockProvider provider;
        provider.enqueue_text("Response");
        tool::ToolRegistry registry;
        std::string output;
        AgentLoop loop(&provider, &registry, nullptr,
                       AgentLoop::Config{},
                       [&](const std::string& t) { output = t; });
        return loop.run("Question");
    };
}

TEST_CASE("Benchmark: AgentLoop tool call round-trip", "[benchmark][agent]") {
    BENCHMARK("tool_round_trip_mock") {
        MockProvider provider;
        provider.enqueue(make_tools({make_call("fast_tool", "c1")}));
        provider.enqueue_text("Done");
        tool::ToolRegistry registry;
        registry.register_tool(std::make_unique<MockTool>("fast_tool", "result", false, false));
        std::string output;
        AgentLoop loop(&provider, &registry, nullptr,
                       AgentLoop::Config{},
                       [&](const std::string& t) { output = t; });
        return loop.run("Use the tool");
    };
}
