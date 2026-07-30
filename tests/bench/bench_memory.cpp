// tests/bench/bench_memory.cpp
// Performance benchmarks for HolographicMemory store/recall
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "memory/HolographicMemory.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("Benchmark: HolographicMemory store", "[benchmark][memory]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    mem.open();
    BENCHMARK("store_100_entries") {
        for (int i = 0; i < 100; ++i) {
            mem.store("fact " + std::to_string(i), "bench", 5);
        }
    };
}

TEST_CASE("Benchmark: HolographicMemory FTS5 recall", "[benchmark][memory]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    mem.open();
    // Pre-populate 1000 entries
    for (int i = 0; i < 1000; ++i) {
        mem.store("document about topic " + std::to_string(i) + " with keywords", "bench", 5);
    }
    BENCHMARK("recall_from_1k") {
        return mem.recall("topic", 10);
    };
}
