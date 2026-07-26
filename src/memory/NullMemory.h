#pragma once
#include "memory/IMemory.h"

namespace ea::memory {

class NullMemory : public IMemory {
public:
    Result<std::string> store(const std::string&, const std::string& = "core", int = 5) override {
        return std::string("null");
    }
    Result<std::vector<MemoryEntry>> recall(const std::string&, int = 10) override {
        return std::vector<MemoryEntry>{};
    }
    Result<bool> forget(const std::string&) override { return true; }
    Result<std::vector<MemoryEntry>> list(int = 50, int = 0) override {
        return std::vector<MemoryEntry>{};
    }
    Result<int> count() override { return 0; }
};

}  // namespace ea::memory
