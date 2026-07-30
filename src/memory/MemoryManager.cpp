#include "MemoryManager.h"
#include "log/Logger.h"

namespace ea::memory {

MemoryManager::MemoryManager(std::unique_ptr<IMemory> backend)
    : backend_(std::move(backend)) {}

std::vector<MemoryEntry> MemoryManager::prefetch(const std::string& user_input) {
    last_user_input_ = user_input;

    // Notify backend of turn start
    backend_->on_turn_start(user_input);

    // Recall relevant memories for this input
    auto result = backend_->recall(user_input);
    if (result.ok()) {
        cached_context_ = std::move(result.value());
    } else {
        cached_context_.clear();
        EA_WARN("Memory prefetch failed: {}", result.error().message);
    }

    return cached_context_;
}

void MemoryManager::sync_turn(const std::string& /*user_input*/,
                               const std::string& assistant_output) {
    // Notify backend of turn end (auto-store if configured)
    backend_->on_turn_end(assistant_output);

    // Clear cached context for next turn
    cached_context_.clear();
}

// Legacy CRUD delegation
Result<std::string> MemoryManager::store(const std::string& content,
                                          const std::string& category,
                                          int importance) {
    return backend_->store(content, category, importance);
}

Result<std::vector<MemoryEntry>> MemoryManager::recall(const std::string& query,
                                                        int limit) {
    return backend_->recall(query, limit);
}

Result<bool> MemoryManager::forget(const std::string& id) {
    return backend_->forget(id);
}

Result<std::vector<MemoryEntry>> MemoryManager::list(int limit, int offset) {
    return backend_->list(limit, offset);
}

Result<int> MemoryManager::count() {
    return backend_->count();
}

std::string MemoryManager::system_prompt_block() const {
    return backend_->system_prompt_block();
}

Result<void> MemoryManager::open() {
    return backend_->open();
}

Result<void> MemoryManager::close() {
    return backend_->close();
}

void MemoryManager::on_pre_compress() {
    backend_->on_pre_compress();
}

// Holographic delegation
Result<int> MemoryManager::add_fact(const std::string& content,
                                      const std::string& category,
                                      const std::string& tags) {
    return backend_->add_fact(content, category, tags);
}

Result<std::vector<FactEntry>> MemoryManager::search_facts(
    const std::string& query, const std::string& category,
    double min_trust, int limit) {
    return backend_->search_facts(query, category, min_trust, limit);
}

Result<bool> MemoryManager::update_fact(int fact_id,
                                          const std::string* content,
                                          const double* trust_delta,
                                          const std::string* tags,
                                          const std::string* category) {
    return backend_->update_fact(fact_id, content, trust_delta, tags, category);
}

Result<bool> MemoryManager::remove_fact(int fact_id) {
    return backend_->remove_fact(fact_id);
}

Result<std::vector<FactEntry>> MemoryManager::list_facts(
    const std::string& category, double min_trust, int limit) {
    return backend_->list_facts(category, min_trust, limit);
}

Result<FeedbackResult> MemoryManager::record_feedback(int fact_id, bool helpful) {
    return backend_->record_feedback(fact_id, helpful);
}

Result<std::vector<FactEntry>> MemoryManager::probe(
    const std::string& entity, const std::string& category, int limit) {
    return backend_->probe(entity, category, limit);
}

Result<std::vector<FactEntry>> MemoryManager::related(
    const std::string& entity, const std::string& category, int limit) {
    return backend_->related(entity, category, limit);
}

Result<std::vector<FactEntry>> MemoryManager::reason(
    const std::vector<std::string>& entities, const std::string& category, int limit) {
    return backend_->reason(entities, category, limit);
}

Result<std::vector<ContradictionPair>> MemoryManager::contradict(
    const std::string& category, double threshold, int limit) {
    return backend_->contradict(category, threshold, limit);
}

bool MemoryManager::is_holographic() const {
    return backend_->is_holographic();
}

}  // namespace ea::memory
