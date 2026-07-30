#pragma once
#include "base/Types.h"
#include "base/Result.h"
#include "memory/FactEntry.h"

namespace ea {

class IMemory {
public:
    virtual ~IMemory() = default;

    // === Legacy CRUD (kept for backward compatibility) ===
    // store() maps to add_fact() in HolographicMemory
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    // recall() maps to search_facts() in HolographicMemory
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    // forget() maps to remove_fact() in HolographicMemory
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;

    // === Lifecycle hooks (default no-op implementations) ===
    virtual Result<void> open() { return {}; }
    virtual Result<void> close() { return {}; }
    virtual std::string system_prompt_block() const { return {}; }

    // Turn-level hooks
    virtual void on_turn_start(const std::string& user_input) { (void)user_input; }
    virtual void on_turn_end(const std::string& assistant_output) { (void)assistant_output; }
    virtual void on_pre_compress() {}

    // === Holographic extensions (default: not supported / no-op) ===

    // Fact CRUD
    virtual Result<int> add_fact(const std::string& content,
                                  const std::string& category = "general",
                                  const std::string& tags = "") {
        (void)content; (void)category; (void)tags;
        return Error{ErrorCode::InvalidArgument, "not supported", 0, {}};
    }
    virtual Result<std::vector<memory::FactEntry>> search_facts(
        const std::string& query, const std::string& category = "",
        double min_trust = 0.3, int limit = 10) {
        (void)query; (void)category; (void)min_trust; (void)limit;
        return std::vector<memory::FactEntry>{};
    }
    virtual Result<bool> update_fact(int fact_id,
                                      const std::string* content = nullptr,
                                      const double* trust_delta = nullptr,
                                      const std::string* tags = nullptr,
                                      const std::string* category = nullptr) {
        (void)fact_id; (void)content; (void)trust_delta; (void)tags; (void)category;
        return false;
    }
    virtual Result<bool> remove_fact(int fact_id) {
        (void)fact_id;
        return false;
    }
    virtual Result<std::vector<memory::FactEntry>> list_facts(
        const std::string& category = "", double min_trust = 0.0,
        int limit = 50) {
        (void)category; (void)min_trust; (void)limit;
        return std::vector<memory::FactEntry>{};
    }

    // Trust feedback
    virtual Result<memory::FeedbackResult> record_feedback(int fact_id, bool helpful) {
        (void)fact_id; (void)helpful;
        return Error{ErrorCode::InvalidArgument, "not supported", 0, {}};
    }

    // Algebraic queries
    virtual Result<std::vector<memory::FactEntry>> probe(
        const std::string& entity, const std::string& category = "",
        int limit = 10) {
        (void)entity; (void)category; (void)limit;
        return std::vector<memory::FactEntry>{};
    }
    virtual Result<std::vector<memory::FactEntry>> related(
        const std::string& entity, const std::string& category = "",
        int limit = 10) {
        (void)entity; (void)category; (void)limit;
        return std::vector<memory::FactEntry>{};
    }
    virtual Result<std::vector<memory::FactEntry>> reason(
        const std::vector<std::string>& entities, const std::string& category = "",
        int limit = 10) {
        (void)entities; (void)category; (void)limit;
        return std::vector<memory::FactEntry>{};
    }
    virtual Result<std::vector<memory::ContradictionPair>> contradict(
        const std::string& category = "", double threshold = 0.3,
        int limit = 10) {
        (void)category; (void)threshold; (void)limit;
        return std::vector<memory::ContradictionPair>{};
    }

    // Capability detection
    virtual bool is_holographic() const { return false; }
};

}  // namespace ea
