// tests/bench/bench_concurrent.cpp
// Performance benchmarks for concurrent AgentLoop instances
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "MockProvider.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include <thread>
#include <vector>
#include <atomic>

using namespace ea;
using namespace ea::test;
using namespace ea::agent;

TEST_CASE("Benchmark: concurrent AgentLoop instances", "[benchmark][concurrent]") {
    BENCHMARK("4_concurrent_loops") {
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&success_count]() {
                MockProvider provider;
                provider.enqueue_text("Response");
                tool::ToolRegistry registry;
                std::string output;
                AgentLoop loop(&provider, &registry, nullptr,
                               AgentLoop::Config{},
                               [&](const std::string& t) { output = t; });
                auto result = loop.run("test");
                if (result.ok()) success_count++;
            });
        }
        for (auto& t : threads) t.join();
        return success_count.load();
    };
}
