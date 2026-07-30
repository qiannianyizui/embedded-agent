#include <catch2/catch_test_macros.hpp>
#include "MockProvider.h"
#include "TestHelpers.h"
#include "ContractTestHelper.h"
#include "memory/HolographicMemory.h"
#include "memory/ScopedMemory.h"

using namespace ea;
using namespace ea::test;
using namespace ea::test::contract;

// ── HolographicMemory satisfies memory contract ────────────────────────────────

TEST_CASE("HolographicMemory satisfies memory contract", "[contract][greybox][memory]") {
    ea::memory::HolographicMemory memory(ea::memory::HolographicMemoryConfig{":memory:", false});
    memory.open();
    MemoryContract::verify_all(memory);
}

// ── HolographicMemory store returns ID, recall works ───────────────────────────

TEST_CASE("HolographicMemory store returns ID, recall works", "[contract][greybox][memory]") {
    ea::memory::HolographicMemory memory(ea::memory::HolographicMemoryConfig{":memory:", false});
    memory.open();

    auto store_result = memory.store("some content", "test", 5);
    REQUIRE(store_result.ok());
    REQUIRE_FALSE(store_result.value().empty());

    auto recall_result = memory.recall("some content", 10);
    REQUIRE(recall_result.ok());
    REQUIRE_FALSE(recall_result.value().empty());
}

// ── ScopedMemory scopes writes by agent_id ───────────────────────────────────

TEST_CASE("ScopedMemory scopes writes by agent_id", "[contract][greybox][memory]") {
    auto backend = std::make_unique<ea::memory::HolographicMemory>(ea::memory::HolographicMemoryConfig{":memory:", false});
    backend->open();
    ea::memory::ScopedMemory memory(
        std::move(backend),
        ea::memory::MemoryScope{"agent-A", "", {}});

    memory.open();

    // Store via ScopedMemory — category should be prefixed with agent_id
    auto store_result = memory.store("scoped content", "notes", 5);
    REQUIRE(store_result.ok());

    // Recall should find the entry (it belongs to agent-A)
    auto recall_result = memory.recall("scoped content", 10);
    REQUIRE(recall_result.ok());
    REQUIRE_FALSE(recall_result.value().empty());

    // The underlying category should be "agent-A:notes"
    const auto& entry = recall_result.value().front();
    REQUIRE(entry.category.find("agent-A:") == 0);
}

// ── ScopedMemory filters reads by scope ──────────────────────────────────────

TEST_CASE("ScopedMemory filters reads by scope", "[contract][greybox][memory]") {
    // Create a shared backend and write entries from two different agents
    auto backend = std::make_unique<ea::memory::HolographicMemory>(ea::memory::HolographicMemoryConfig{":memory:", false});
    ea::memory::HolographicMemory* raw_backend = backend.get();
    raw_backend->open();

    // Create ScopedMemory for agent-A with no allowlist
    ea::memory::ScopedMemory memory_a(
        std::move(backend),
        ea::memory::MemoryScope{"agent-A", "", {}});

    memory_a.open();

    // Store as agent-A
    memory_a.store("agent-A content", "notes", 5);

    // Directly store as agent-B in the raw backend
    raw_backend->store("agent-B content", "agent-B:notes", 5);

    // agent-A recall should only see its own entries, not agent-B's
    auto recall_result = memory_a.recall("content", 10);
    REQUIRE(recall_result.ok());

    for (const auto& entry : recall_result.value()) {
        // Every returned entry must be readable by agent-A
        // Either it starts with "agent-A:" or has no colon (unscoped)
        bool own = entry.category.find("agent-A:") == 0;
        bool unscoped = entry.category.find(":") == std::string::npos;
        REQUIRE((own || unscoped));
    }

    // Now create a ScopedMemory for agent-C with agent-B in its allowlist
    auto backend2 = std::make_unique<ea::memory::HolographicMemory>(ea::memory::HolographicMemoryConfig{":memory:", false});
    ea::memory::HolographicMemory* raw2 = backend2.get();
    raw2->open();
    raw2->store("agent-B shared content", "agent-B:shared", 5);
    raw2->store("agent-C own content", "agent-C:own", 5);
    raw2->store("unscoped content", "general", 5);

    ea::memory::ScopedMemory memory_c(
        std::move(backend2),
        ea::memory::MemoryScope{"agent-C", "", {"agent-B"}});

    memory_c.open();

    auto recall_c = memory_c.recall("content", 10);
    REQUIRE(recall_c.ok());
    // agent-C should see: its own entries, agent-B's entries, and unscoped entries
    int count = static_cast<int>(recall_c.value().size());
    REQUIRE(count == 3);
}
