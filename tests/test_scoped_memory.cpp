#include <catch2/catch_test_macros.hpp>
#include "memory/ScopedMemory.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("ScopedMemory store attaches agent_id to category", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    auto id = scoped.store("my thought", "core", 5);
    REQUIRE(id.ok());

    // Verify the backend stored with scoped category
    auto list = scoped.list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 1);
    REQUIRE(list.value()[0].category == "agent-A:core");
}

TEST_CASE("ScopedMemory can read own agent entries", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    scoped.store("my thought", "core", 5);
    auto results = scoped.recall("thought");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
}

TEST_CASE("ScopedMemory blocks other agent entries", "[memory][scoped]") {
    // Create shared backend, store from agent-B, then read from agent-A
    auto backend = std::make_unique<InMemoryBackend>();

    // Store from agent-B directly into backend
    backend->store("agent-B secret", "agent-B:core", 5);
    backend->store("unscoped entry", "core", 5);

    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    auto results = scoped.recall("entry");
    REQUIRE(results.ok());
    // Should only see unscoped entry, not agent-B's
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "unscoped entry");
}

TEST_CASE("ScopedMemory read_allowlist allows cross-agent reads", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("shared entry from B", "agent-B:core", 5);
    backend->store("unscoped entry", "core", 5);

    MemoryScope scope;
    scope.agent_id = "agent-A";
    scope.read_allowlist = {"agent-B"};
    ScopedMemory scoped(std::move(backend), scope);

    auto results = scoped.recall("entry");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 2);
}

TEST_CASE("ScopedMemory lifecycle delegates to backend", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    REQUIRE(scoped.open().ok());
    REQUIRE(scoped.close().ok());
}

TEST_CASE("ScopedMemory rejects empty agent_id", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "";  // empty
    REQUIRE_THROWS_AS(ScopedMemory(std::move(backend), scope), std::invalid_argument);
}
