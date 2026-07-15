#pragma once
#include "core/IMemory.h"
#include <vector>
#include <string>

namespace ea::memory {

class InMemoryBackend : public IMemory {
public:
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;
    std::string system_prompt_block() const override;

private:
    std::vector<MemoryEntry> entries_;
    int next_id_ = 1;
};

}  // namespace ea::memory
