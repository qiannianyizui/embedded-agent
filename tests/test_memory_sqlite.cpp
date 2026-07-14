#include <catch2/catch_test_macros.hpp>
#include "memory/SqliteMemory.h"

using namespace ea::memory;

TEST_CASE("SqliteMemory store and recall", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);
    auto id = mem.store("test content about C++ agents", "core", 7);
    REQUIRE(id.ok());
    auto results = mem.recall("C++ agents");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "test content about C++ agents");
    REQUIRE(results.value()[0].importance == 7);
}

TEST_CASE("SqliteMemory forget", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);
    auto id = mem.store("to be deleted", "core", 3);
    REQUIRE(id.ok());
    auto del = mem.forget(id.value());
    REQUIRE(del.ok());
    REQUIRE(del.value() == true);
    auto c = mem.count();
    REQUIRE(c.ok());
    REQUIRE(c.value() == 0);
}

TEST_CASE("SqliteMemory list", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);
    mem.store("entry 1", "core", 5);
    mem.store("entry 2", "daily", 3);
    auto list = mem.list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
}

TEST_CASE("SqliteMemory count", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);
    REQUIRE(mem.count().ok());
    REQUIRE(mem.count().value() == 0);
    mem.store("a", "core", 5);
    mem.store("b", "core", 5);
    REQUIRE(mem.count().value() == 2);
}

TEST_CASE("SqliteMemory FTS5 search", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    cfg.enable_fts5 = true;
    SqliteMemory mem(cfg);
    mem.store("The quick brown fox jumps over the lazy dog", "core", 5);
    mem.store("A brown bear walks in the forest", "core", 5);
    mem.store("The cat sleeps on the mat", "core", 5);
    auto results = mem.recall("brown fox");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() >= 1);
    REQUIRE(results.value()[0].content.find("fox") != std::string::npos);
}
