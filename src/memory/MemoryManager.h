// MemoryManager — orchestration layer for IMemory backends
// Handles turn-level prefetch/sync, system prompt injection, and lifecycle.
#pragma once
#include "core/IMemory.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::memory {

class MemoryManager {
public:
    explicit MemoryManager(std::unique_ptr<IMemory> backend);

    // Turn lifecycle
    std::vector<MemoryEntry> prefetch(const std::string& user_input);
    void sync_turn(const std::string& user_input,
                   const std::string& assistant_output);

    // Delegate to underlying IMemory
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5);
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10);
    Result<bool> forget(const std::string& id);
    Result<int> count();

    // System prompt injection
    std::string build_memory_block() const;

    // Lifecycle
    Result<void> open();
    Result<void> close();
    void on_pre_compress();

    // Access the underlying backend (for MemoryTool integration)
    IMemory* backend() const { return backend_.get(); }

private:
    std::unique_ptr<IMemory> backend_;
    std::vector<MemoryEntry> cached_context_;
    std::string last_user_input_;
};

}  // namespace ea::memory
