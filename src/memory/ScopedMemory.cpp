#include "ScopedMemory.h"

namespace ea::memory {

ScopedMemory::ScopedMemory(std::unique_ptr<IMemory> backend, MemoryScope scope)
    : backend_(std::move(backend)), scope_(std::move(scope)) {
    if (scope_.agent_id.empty()) {
        throw std::invalid_argument("ScopedMemory requires non-empty agent_id");
    }
}

Result<std::string> ScopedMemory::store(const std::string& content,
                                         const std::string& category,
                                         int importance) {
    // Store with agent_id attached via category prefix
    std::string scoped_category = scope_.agent_id + ":" + category;
    return backend_->store(content, scoped_category, importance);
}

Result<std::vector<MemoryEntry>> ScopedMemory::recall(const std::string& query,
                                                       int limit) {
    auto result = backend_->recall(query, limit);
    if (!result.ok()) return result;

    // Filter to own agent's entries + allowlisted agents
    std::vector<MemoryEntry> filtered;
    for (const auto& entry : result.value()) {
        if (can_read(entry)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

Result<bool> ScopedMemory::forget(const std::string& id) {
    return backend_->forget(id);
}

Result<std::vector<MemoryEntry>> ScopedMemory::list(int limit, int offset) {
    auto result = backend_->list(limit, offset);
    if (!result.ok()) return result;

    std::vector<MemoryEntry> filtered;
    for (const auto& entry : result.value()) {
        if (can_read(entry)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

Result<int> ScopedMemory::count() {
    return backend_->count();
}

bool ScopedMemory::can_read(const MemoryEntry& entry) const {
    // Own agent's entries (category starts with our agent_id)
    if (entry.category.find(scope_.agent_id + ":") == 0) return true;
    // Allowlisted agents' entries
    for (const auto& allowed_id : scope_.read_allowlist) {
        if (entry.category.find(allowed_id + ":") == 0) return true;
    }
    // Entries without agent prefix (unscoped) are readable by all
    if (entry.category.find(":") == std::string::npos) return true;
    return false;
}

// Lifecycle delegation
Result<void> ScopedMemory::open() { return backend_->open(); }
Result<void> ScopedMemory::close() { return backend_->close(); }
std::string ScopedMemory::system_prompt_block() const { return backend_->system_prompt_block(); }
void ScopedMemory::on_turn_start(const std::string& user_input) { backend_->on_turn_start(user_input); }
void ScopedMemory::on_turn_end(const std::string& assistant_output) { backend_->on_turn_end(assistant_output); }
void ScopedMemory::on_pre_compress() { backend_->on_pre_compress(); }

// Holographic delegation
Result<int> ScopedMemory::add_fact(const std::string& content,
                                     const std::string& category,
                                     const std::string& tags) {
    std::string scoped_category = scope_.agent_id + ":" + category;
    return backend_->add_fact(content, scoped_category, tags);
}

Result<std::vector<FactEntry>> ScopedMemory::search_facts(
    const std::string& query, const std::string& category,
    double min_trust, int limit) {
    return backend_->search_facts(query, category, min_trust, limit);
}

Result<bool> ScopedMemory::update_fact(int fact_id,
                                         const std::string* content,
                                         const double* trust_delta,
                                         const std::string* tags,
                                         const std::string* category) {
    return backend_->update_fact(fact_id, content, trust_delta, tags, category);
}

Result<bool> ScopedMemory::remove_fact(int fact_id) {
    return backend_->remove_fact(fact_id);
}

Result<std::vector<FactEntry>> ScopedMemory::list_facts(
    const std::string& category, double min_trust, int limit) {
    return backend_->list_facts(category, min_trust, limit);
}

Result<FeedbackResult> ScopedMemory::record_feedback(int fact_id, bool helpful) {
    return backend_->record_feedback(fact_id, helpful);
}

Result<std::vector<FactEntry>> ScopedMemory::probe(
    const std::string& entity, const std::string& category, int limit) {
    return backend_->probe(entity, category, limit);
}

Result<std::vector<FactEntry>> ScopedMemory::related(
    const std::string& entity, const std::string& category, int limit) {
    return backend_->related(entity, category, limit);
}

Result<std::vector<FactEntry>> ScopedMemory::reason(
    const std::vector<std::string>& entities, const std::string& category, int limit) {
    return backend_->reason(entities, category, limit);
}

Result<std::vector<ContradictionPair>> ScopedMemory::contradict(
    const std::string& category, double threshold, int limit) {
    return backend_->contradict(category, threshold, limit);
}

bool ScopedMemory::is_holographic() const {
    return backend_->is_holographic();
}

}  // namespace ea::memory
