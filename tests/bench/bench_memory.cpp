// tests/bench/bench_memory.cpp
// Performance benchmarks for InMemoryBackend and SqliteMemory store/recall
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "memory/SqliteMemory.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("Benchmark: InMemoryBackend store", "[benchmark][memory]") {
    InMemoryBackend mem;
    BENCHMARK("store_100_entries") {
        for (int i = 0; i < 100; ++i) {
            mem.store("fact " + std::to_string(i), "bench", 5);
        }
    };
}

TEST_CASE("Benchmark: SqliteMemory store", "[benchmark][memory]") {
    SqliteMemory mem(SqliteMemory::Config{":memory:", true, true});
    mem.open();
    BENCHMARK("store_100_entries") {
        for (int i = 0; i < 100; ++i) {
            mem.store("fact " + std::to_string(i), "bench", 5);
        }
    };
}

TEST_CASE("Benchmark: SqliteMemory FTS5 recall", "[benchmark][memory]") {
    SqliteMemory mem(SqliteMemory::Config{":memory:", true, true});
    mem.open();
    // Pre-populate 1000 entries
    for (int i = 0; i < 1000; ++i) {
        mem.store("document about topic " + std::to_string(i) + " with keywords", "bench", 5);
    }
    BENCHMARK("recall_from_1k") {
        return mem.recall("topic", 10);
    };
}
