// MemoryManager — orchestration layer for IMemory backends
// Inherits IMemory so it can be used wherever IMemory* is expected.
// Adds turn-level prefetch/sync and system prompt injection.
#pragma once
#include "memory/IMemory.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::memory {

class MemoryManager : public IMemory {
public:
    explicit MemoryManager(std::unique_ptr<IMemory> backend);

    // IMemory interface — delegated to backend
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    // Lifecycle — delegated to backend
    Result<void> open() override;
    Result<void> close() override;
    std::string system_prompt_block() const override;
    void on_pre_compress() override;

    // Turn lifecycle (MemoryManager extensions)
    std::vector<MemoryEntry> prefetch(const std::string& user_input);
    void sync_turn(const std::string& user_input,
                   const std::string& assistant_output);

    // Access the underlying backend (for MemoryTool integration)
    IMemory* backend() const { return backend_.get(); }

private:
    std::unique_ptr<IMemory> backend_;
    std::vector<MemoryEntry> cached_context_;
    std::string last_user_input_;
};

}  // namespace ea::memory
