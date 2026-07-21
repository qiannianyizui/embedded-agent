#include <catch2/catch_test_macros.hpp>
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("InMemoryBackend store and recall", "[memory][inmemory]") {
    InMemoryBackend mem;
    auto id = mem.store("test content about C++", "core", 7);
    REQUIRE(id.ok());
    auto results = mem.recall("C++");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "test content about C++");
}

TEST_CASE("InMemoryBackend forget", "[memory][inmemory]") {
    InMemoryBackend mem;
    auto id = mem.store("to be deleted", "core", 3);
    REQUIRE(id.ok());
    auto del = mem.forget(id.value());
    REQUIRE(del.ok());
    REQUIRE(del.value() == true);
    REQUIRE(mem.count().value() == 0);
}

TEST_CASE("InMemoryBackend count", "[memory][inmemory]") {
    InMemoryBackend mem;
    REQUIRE(mem.count().value() == 0);
    mem.store("a", "core", 5);
    mem.store("b", "core", 5);
    REQUIRE(mem.count().value() == 2);
}

TEST_CASE("InMemoryBackend system_prompt_block", "[memory][inmemory]") {
    InMemoryBackend mem;
    REQUIRE(mem.system_prompt_block().empty());
    mem.store("user prefers dark mode", "preference", 7);
    auto block = mem.system_prompt_block();
    REQUIRE(block.find("Relevant Memories") != std::string::npos);
    REQUIRE(block.find("dark mode") != std::string::npos);
}

TEST_CASE("InMemoryBackend recall with limit", "[memory][inmemory]") {
    InMemoryBackend mem;
    mem.store("entry 1", "core", 5);
    mem.store("entry 2", "core", 5);
    mem.store("entry 3", "core", 5);
    auto results = mem.recall("entry", 2);
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 2);
}
