#include <catch2/catch_test_macros.hpp>
#include "memory/MemoryFactory.h"

using namespace ea::memory;

TEST_CASE("MemoryFactory creates InMemory backend", "[memory][factory]") {
    auto mem = create_backend(MemoryBackendType::InMemory);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
}

TEST_CASE("MemoryFactory creates Null backend", "[memory][factory]") {
    auto mem = create_backend(MemoryBackendType::Null);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
    REQUIRE(mem->count().value() == 0);
}

TEST_CASE("MemoryFactory creates Sqlite backend", "[memory][factory]") {
    MemoryBackendConfig cfg;
    cfg.path = ":memory:";
    auto mem = create_backend(MemoryBackendType::Sqlite, cfg);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
}
