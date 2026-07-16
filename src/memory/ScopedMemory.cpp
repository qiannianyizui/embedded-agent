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

}  // namespace ea::memory
