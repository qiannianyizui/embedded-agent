// ScopedMemory — agent-scoped memory decorator
// Wraps an IMemory backend, scoping reads/writes by agent_id.
#pragma once
#include "core/IMemory.h"
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

private:
    bool can_read(const MemoryEntry& entry) const;

    std::unique_ptr<IMemory> backend_;
    MemoryScope scope_;
};

}  // namespace ea::memory
