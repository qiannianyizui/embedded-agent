#include <catch2/catch_test_macros.hpp>
#include "memory/MemoryManager.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("MemoryManager prefetch returns relevant memories", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("C++ is great", "core", 5);
    backend->store("Python is nice", "core", 5);

    MemoryManager mgr(std::move(backend));
    auto results = mgr.prefetch("C++");
    REQUIRE(results.size() >= 1);
    REQUIRE(results[0].content.find("C++") != std::string::npos);
}

TEST_CASE("MemoryManager store and recall", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    auto id = mgr.store("test content", "core", 5);
    REQUIRE(id.ok());

    auto results = mgr.recall("test");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
}

TEST_CASE("MemoryManager forget", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    auto id = mgr.store("to delete", "core", 3);
    REQUIRE(id.ok());
    auto result = mgr.forget(id.value());
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
}

TEST_CASE("MemoryManager count", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.count().value() == 0);
    mgr.store("a", "core", 5);
    mgr.store("b", "core", 5);
    REQUIRE(mgr.count().value() == 2);
}

TEST_CASE("MemoryManager system_prompt_block", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.system_prompt_block().empty());

    mgr.store("important fact", "core", 7);
    auto block = mgr.system_prompt_block();
    REQUIRE_FALSE(block.empty());
}

TEST_CASE("MemoryManager sync_turn clears cached context", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("some memory", "core", 5);

    MemoryManager mgr(std::move(backend));
    auto results = mgr.prefetch("memory");
    REQUIRE(results.size() >= 1);

    mgr.sync_turn("user input", "assistant output");
    // After sync, next prefetch should work fresh
    auto results2 = mgr.prefetch("memory");
    REQUIRE(results2.size() >= 1);
}

TEST_CASE("MemoryManager open and close", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.open().ok());
    REQUIRE(mgr.close().ok());
}

TEST_CASE("MemoryManager backend accessor", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.backend() != nullptr);
}
