// ScopedMemory — agent-scoped memory decorator
// Wraps an IMemory backend, scoping reads/writes by agent_id.
#pragma once
#include "memory/IMemory.h"
#include <set>
#include <string>

namespace ea::memory {

struct MemoryScope {
    std::string agent_id;
    std::string session_id;
    std::set<std::string> read_allowlist;
};

class ScopedMemory : public IMemory {
public:
    ScopedMemory(std::unique_ptr<IMemory> backend, MemoryScope scope);

    // Legacy CRUD
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    // Lifecycle delegation
    Result<void> open() override;
    Result<void> close() override;
    std::string system_prompt_block() const override;
    void on_turn_start(const std::string& user_input) override;
    void on_turn_end(const std::string& assistant_output) override;
    void on_pre_compress() override;

    // Holographic delegation — forward to backend
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
    Result<FeedbackResult> record_feedback(int fact_id, bool helpful) override;
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
    bool is_holographic() const override;

private:
    bool can_read(const MemoryEntry& entry) const;

    std::unique_ptr<IMemory> backend_;
    MemoryScope scope_;
};

}  // namespace ea::memory
