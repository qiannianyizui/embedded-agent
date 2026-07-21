// tests/bench/bench_tool_execute.cpp
// Performance benchmarks for tool execution
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "MockTool.h"

using namespace ea::test;

TEST_CASE("Benchmark: MockTool execute", "[benchmark][tool]") {
    MockTool tool("bench_tool", "result");
    nlohmann::json args = {{"key", "value"}};
    BENCHMARK("execute_mock_tool") {
        return tool.execute(args);
    };
}
