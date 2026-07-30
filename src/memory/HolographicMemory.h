#pragma once
// HolographicMemory — IMemory implementation with structured fact storage,
// entity knowledge graph, trust scoring, and hybrid retrieval.
// Replaces SqliteMemory/InMemoryBackend/NullMemory as the sole backend.

#include "memory/IMemory.h"
#include "memory/FactEntry.h"
#include "memory/EntityExtractor.h"
#include "memory/FactRetriever.h"
#include <string>
#include <memory>

struct sqlite3;

namespace ea::memory {

struct HolographicMemoryConfig {
    std::string path = ":memory:";     // SQLite database path
    bool enable_wal = true;            // WAL mode for file-based DBs
    double trust_positive = 0.05;      // Trust delta for helpful feedback
    double trust_negative = -0.10;     // Trust delta for unhelpful feedback
    double trust_floor = 0.0;          // Minimum trust score
    double trust_ceiling = 1.0;        // Maximum trust score
    int hrr_dim = 1024;               // HRR vector dimension (Phase 2)
};

class HolographicMemory : public ea::IMemory {
public:
    explicit HolographicMemory(HolographicMemoryConfig config = HolographicMemoryConfig{});
    ~HolographicMemory() override;

    HolographicMemory(const HolographicMemory&) = delete;
    HolographicMemory& operator=(const HolographicMemory&) = delete;

    // === Legacy IMemory CRUD (mapped to fact operations) ===
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    // === Lifecycle ===
    Result<void> open() override;
    Result<void> close() override;
    std::string system_prompt_block() const override;
    void on_turn_start(const std::string& user_input) override;
    void on_turn_end(const std::string& assistant_output) override;
    void on_pre_compress() override;

    // === Holographic extensions ===
    Result<int> add_fact(const std::string& content,
                          const std::string& category = "general",
                          const std::string& tags = "") override;
    Result<std::vector<FactEntry>> search_facts(
        const std::string& query, const std::string& category = "",
        double min_trust = 0.3, int limit = 10) override;
    Result<bool> update_fact(int fact_id,
                              const std::string* content = nullptr,
                              const double* trust_delta = nullptr,
                              const std::string* tags = nullptr,
                              const std::string* category = nullptr) override;
    Result<bool> remove_fact(int fact_id) override;
    Result<std::vector<FactEntry>> list_facts(
        const std::string& category = "", double min_trust = 0.0,
        int limit = 50) override;

    // Trust feedback
    Result<FeedbackResult> record_feedback(int fact_id, bool helpful) override;

    // Algebraic queries
    Result<std::vector<FactEntry>> probe(
        const std::string& entity, const std::string& category = "",
        int limit = 10) override;
    Result<std::vector<FactEntry>> related(
        const std::string& entity, const std::string& category = "",
        int limit = 10) override;
    Result<std::vector<FactEntry>> reason(
        const std::vector<std::string>& entities, const std::string& category = "",
        int limit = 10) override;
    Result<std::vector<ContradictionPair>> contradict(
        const std::string& category = "", double threshold = 0.3,
        int limit = 10) override;

    // Capability
    bool is_holographic() const override { return true; }

    // === Statistics ===
    struct Stats {
        int total_facts = 0;
        int total_entities = 0;
        int64_t db_size_bytes = 0;
        int low_trust_facts = 0;   // trust < 0.3
        int high_trust_facts = 0;  // trust >= 0.7
    };
    Result<Stats> stats() const;

private:
    Result<void> create_tables();
    Result<void> ensure_entity(const std::string& name, const std::string& entity_type = "auto");
    Result<void> link_fact_entity(int fact_id, const std::string& entity_name);
    Result<std::vector<std::string>> get_fact_entities(int fact_id) const;
    FactEntry row_to_fact(void* stmt) const;

    HolographicMemoryConfig config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
    std::unique_ptr<FactRetriever> retriever_;
    EntityExtractor entity_extractor_;
};

}  // namespace ea::memory
