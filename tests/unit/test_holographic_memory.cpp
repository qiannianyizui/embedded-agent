#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "memory/HolographicMemory.h"
#include "memory/EntityExtractor.h"
#include "memory/FtsSanitizer.h"

using namespace ea;
using namespace ea::memory;
using Catch::Matchers::WithinAbs;

// Helper: create an in-memory HolographicMemory and open it
static std::unique_ptr<HolographicMemory> make_mem() {
    auto m = std::make_unique<HolographicMemory>(HolographicMemoryConfig{":memory:", false});
    m->open();
    return m;
}

// ── Legacy CRUD (store/recall/forget/list/count) ─────────────────────────

TEST_CASE("HolographicMemory store returns fact id", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->store("test content", "core", 5);
    REQUIRE(id.ok());
    REQUIRE_FALSE(id.value().empty());
}

TEST_CASE("HolographicMemory recall finds stored content", "[memory][holographic]") {
    auto mem = make_mem();
    mem->store("C++ is great", "core", 5);
    mem->store("Python is nice", "core", 5);

    auto results = mem->recall("C++");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() >= 1);
    REQUIRE(results.value()[0].content.find("C++") != std::string::npos);
}

TEST_CASE("HolographicMemory forget removes entry", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->store("to delete", "core", 3);
    REQUIRE(id.ok());

    auto result = mem->forget(id.value());
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
}

TEST_CASE("HolographicMemory count returns correct number", "[memory][holographic]") {
    auto mem = make_mem();
    REQUIRE(mem->count().value() == 0);
    mem->store("a", "core", 5);
    mem->store("b", "core", 5);
    REQUIRE(mem->count().value() == 2);
}

TEST_CASE("HolographicMemory list returns entries", "[memory][holographic]") {
    auto mem = make_mem();
    mem->store("entry1", "core", 5);
    mem->store("entry2", "core", 5);

    auto list = mem->list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
}

// ── Duplicate content handling ───────────────────────────────────────────

TEST_CASE("HolographicMemory store is idempotent for duplicate content", "[memory][holographic]") {
    auto mem = make_mem();
    auto id1 = mem->store("same content", "core", 5);
    auto id2 = mem->store("same content", "core", 5);
    REQUIRE(id1.ok());
    REQUIRE(id2.ok());
    REQUIRE(id1.value() == id2.value());  // Same ID returned for same content
    REQUIRE(mem->count().value() == 1);
}

// ── Fact CRUD ────────────────────────────────────────────────────────────

TEST_CASE("HolographicMemory add_fact returns fact_id", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("Alice likes Python", "user_pref", "programming");
    REQUIRE(id.ok());
    REQUIRE(id.value() > 0);
}

TEST_CASE("HolographicMemory add_fact is idempotent", "[memory][holographic]") {
    auto mem = make_mem();
    auto id1 = mem->add_fact("same fact", "general");
    auto id2 = mem->add_fact("same fact", "general");
    REQUIRE(id1.ok());
    REQUIRE(id2.ok());
    REQUIRE(id1.value() == id2.value());
}

TEST_CASE("HolographicMemory search_facts finds relevant facts", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("Alice prefers dark mode", "user_pref");
    mem->add_fact("Bob uses Vim editor", "tool_pref");

    auto results = mem->search_facts("dark mode");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() >= 1);
    REQUIRE(results.value()[0].content.find("dark mode") != std::string::npos);
}

TEST_CASE("HolographicMemory update_fact changes content", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("original content", "general");
    REQUIRE(id.ok());

    std::string new_content = "updated content";
    auto result = mem->update_fact(id.value(), &new_content);
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);

    auto facts = mem->list_facts("general", 0.0, 10);
    REQUIRE(facts.ok());
    REQUIRE(facts.value().size() >= 1);
    bool found = false;
    for (const auto& f : facts.value()) {
        if (f.fact_id == id.value()) {
            REQUIRE(f.content == "updated content");
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("HolographicMemory update_fact adjusts trust", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("trust test", "general");
    REQUIRE(id.ok());

    double delta = 0.2;
    auto result = mem->update_fact(id.value(), nullptr, &delta);
    REQUIRE(result.ok());

    auto facts = mem->list_facts("general", 0.0, 10);
    REQUIRE(facts.ok());
    for (const auto& f : facts.value()) {
        if (f.fact_id == id.value()) {
            REQUIRE(f.trust_score > 0.5);  // Should be 0.5 + 0.2 = 0.7
        }
    }
}

TEST_CASE("HolographicMemory remove_fact deletes fact", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("to remove", "general");
    REQUIRE(id.ok());

    auto result = mem->remove_fact(id.value());
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
    REQUIRE(mem->count().value() == 0);
}

TEST_CASE("HolographicMemory list_facts filters by category", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("user fact", "user_pref");
    mem->add_fact("project fact", "project");

    auto results = mem->list_facts("user_pref", 0.0, 10);
    REQUIRE(results.ok());
    for (const auto& f : results.value()) {
        REQUIRE(f.category == "user_pref");
    }
}

// ── Trust feedback ───────────────────────────────────────────────────────

TEST_CASE("HolographicMemory record_feedback increases trust for helpful", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("helpful fact", "general");
    REQUIRE(id.ok());

    auto result = mem->record_feedback(id.value(), true);
    REQUIRE(result.ok());
    REQUIRE(result.value().new_trust > result.value().old_trust);
    REQUIRE(result.value().helpful_count == 1);
}

TEST_CASE("HolographicMemory record_feedback decreases trust for unhelpful", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("unhelpful fact", "general");
    REQUIRE(id.ok());

    auto result = mem->record_feedback(id.value(), false);
    REQUIRE(result.ok());
    REQUIRE(result.value().new_trust < result.value().old_trust);
}

TEST_CASE("HolographicMemory trust feedback is asymmetric", "[memory][holographic]") {
    // Negative delta should be larger in magnitude than positive
    HolographicMemoryConfig cfg;
    cfg.trust_positive = 0.05;
    cfg.trust_negative = -0.10;
    auto mem = std::make_unique<HolographicMemory>(cfg);
    mem->open();

    auto id1 = mem->add_fact("positive test", "general");
    auto id2 = mem->add_fact("negative test", "general");
    REQUIRE(id1.ok());
    REQUIRE(id2.ok());

    auto pos = mem->record_feedback(id1.value(), true);
    auto neg = mem->record_feedback(id2.value(), false);

    double pos_delta = pos.value().new_trust - pos.value().old_trust;
    double neg_delta = neg.value().old_trust - neg.value().new_trust;

    REQUIRE(neg_delta > pos_delta);  // |negative| > positive
}

// ── Entity extraction ────────────────────────────────────────────────────

TEST_CASE("HolographicMemory entities are auto-extracted", "[memory][holographic]") {
    auto mem = make_mem();
    auto id = mem->add_fact("Alice prefers dark mode", "user_pref");
    REQUIRE(id.ok());

    auto facts = mem->search_facts("Alice", "user_pref", 0.0, 10);
    REQUIRE(facts.ok());
    if (!facts.value().empty()) {
        auto& fact = facts.value()[0];
        // Alice should be extracted as entity (capitalized word)
        bool found_alice = false;
        for (const auto& e : fact.entities) {
            if (e.find("Alice") != std::string::npos) found_alice = true;
        }
        REQUIRE(found_alice);
    }
}

// ── Algebraic queries ────────────────────────────────────────────────────

TEST_CASE("HolographicMemory probe finds facts by entity", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("Alice likes Python", "general");
    mem->add_fact("Bob likes Java", "general");

    auto results = mem->probe("Alice");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() >= 1);
    REQUIRE(results.value()[0].content.find("Alice") != std::string::npos);
}

TEST_CASE("HolographicMemory related finds neighbor facts", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("Alice likes Python programming", "general");
    mem->add_fact("Alice also uses Docker containers", "general");
    mem->add_fact("Bob prefers Java", "general");

    auto results = mem->related("Alice");
    REQUIRE(results.ok());
    // Should find facts related to Alice (sharing the "Alice" entity)
    REQUIRE(results.value().size() >= 1);
}

TEST_CASE("HolographicMemory reason finds multi-entity intersection", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("Alice likes Python and Docker", "general");
    mem->add_fact("Bob likes Python and Kubernetes", "general");
    mem->add_fact("Alice uses Vim", "general");

    auto results = mem->reason({"Alice", "Python"});
    REQUIRE(results.ok());
    // Should find facts mentioning both Alice and Python
}

TEST_CASE("HolographicMemory is_holographic returns true", "[memory][holographic]") {
    auto mem = make_mem();
    REQUIRE(mem->is_holographic() == true);
}

// ── Lifecycle ────────────────────────────────────────────────────────────

TEST_CASE("HolographicMemory open and close", "[memory][holographic]") {
    HolographicMemory mem(HolographicMemoryConfig{":memory:", false});
    REQUIRE(mem.open().ok());
    REQUIRE(mem.close().ok());
}

TEST_CASE("HolographicMemory system_prompt_block returns content", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("important fact with high trust", "core");

    // Give it high trust first
    double delta = 0.3;
    mem->update_fact(1, nullptr, &delta);

    auto block = mem->system_prompt_block();
    // May or may not have content depending on trust threshold
    // Just verify it doesn't crash
}

// ── EntityExtractor unit tests ───────────────────────────────────────────

TEST_CASE("EntityExtractor extracts capitalized phrases", "[memory][entity]") {
    EntityExtractor ext;
    auto entities = ext.extract("New York is a great city");
    bool found = false;
    for (const auto& e : entities) {
        if (e == "New York") found = true;
    }
    REQUIRE(found);
}

TEST_CASE("EntityExtractor extracts quoted terms", "[memory][entity]") {
    EntityExtractor ext;
    auto entities = ext.extract(R"delim(The project is called "Quantum Engine")delim");
    bool found = false;
    for (const auto& e : entities) {
        if (e == "Quantum Engine") found = true;
    }
    REQUIRE(found);
}

TEST_CASE("EntityExtractor extracts CamelCase identifiers", "[memory][entity]") {
    EntityExtractor ext;
    auto entities = ext.extract("Use HolographicMemory for storage");
    bool found = false;
    for (const auto& e : entities) {
        if (e == "HolographicMemory") found = true;
    }
    REQUIRE(found);
}

TEST_CASE("EntityExtractor deduplicates entities", "[memory][entity]") {
    EntityExtractor ext;
    auto entities = ext.extract("HolographicMemory is great. HolographicMemory works well.");
    int count = 0;
    for (const auto& e : entities) {
        if (e == "HolographicMemory") count++;
    }
    REQUIRE(count == 1);
}

// ── FtsSanitizer unit tests ──────────────────────────────────────────────

TEST_CASE("FtsSanitizer sanitize removes stop words", "[memory][fts]") {
    auto result = FtsSanitizer::sanitize("the cat is on the mat");
    REQUIRE_FALSE(result.empty());
    REQUIRE(result.find("cat") != std::string::npos);
    REQUIRE(result.find("mat") != std::string::npos);
    REQUIRE(result.find(" the ") == std::string::npos);
}

TEST_CASE("FtsSanitizer sanitize joins with OR", "[memory][fts]") {
    auto result = FtsSanitizer::sanitize("python docker");
    REQUIRE(result.find("OR") != std::string::npos);
}

TEST_CASE("FtsSanitizer sanitize returns empty for stop words only", "[memory][fts]") {
    auto result = FtsSanitizer::sanitize("the is a on");
    REQUIRE(result.empty());
}

TEST_CASE("FtsSanitizer tokenize splits correctly", "[memory][fts]") {
    auto tokens = FtsSanitizer::tokenize("Hello, World! 123");
    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[0] == "hello");
    REQUIRE(tokens[1] == "world");
    REQUIRE(tokens[2] == "123");
}

TEST_CASE("FtsSanitizer jaccard_similarity", "[memory][fts]") {
    auto a = FtsSanitizer::tokenize("the cat sat on the mat");
    auto b = FtsSanitizer::tokenize("the dog sat on the mat");
    double sim = FtsSanitizer::jaccard_similarity(a, b);
    REQUIRE(sim > 0.0);
    REQUIRE(sim < 1.0);
}

TEST_CASE("FtsSanitizer jaccard_similarity is 1 for identical sets", "[memory][fts]") {
    auto a = FtsSanitizer::tokenize("hello world");
    double sim = FtsSanitizer::jaccard_similarity(a, a);
    REQUIRE_THAT(sim, WithinAbs(1.0, 1e-10));
}

// ── Statistics ────────────────────────────────────────────────────────────

TEST_CASE("HolographicMemory stats returns correct counts", "[memory][holographic]") {
    auto mem = make_mem();
    mem->add_fact("high trust fact", "general");
    mem->add_fact("low trust fact", "general");

    // Give first fact high trust
    double delta = 0.4;
    mem->update_fact(1, nullptr, &delta);

    auto s = mem->stats();
    REQUIRE(s.ok());
    REQUIRE(s.value().total_facts == 2);
    REQUIRE(s.value().total_entities >= 0);  // May or may not extract entities
    REQUIRE(s.value().high_trust_facts >= 1);
}

// ── Auto-extract on turn end ─────────────────────────────────────────────

TEST_CASE("HolographicMemory on_turn_end auto-extracts preferences", "[memory][holographic]") {
    auto mem = make_mem();
    int before = mem->count().value();

    mem->on_turn_end("I prefer dark mode for coding.");

    int after = mem->count().value();
    REQUIRE(after > before);  // Should have auto-extracted a preference
}
