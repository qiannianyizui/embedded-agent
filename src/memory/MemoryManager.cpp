#include "MemoryManager.h"
#include "common/io/Logger.h"

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

Result<int> MemoryManager::count() {
    return backend_->count();
}

std::string MemoryManager::build_memory_block() const {
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

}  // namespace ea::memory
