#pragma once
#include "Types.h"
#include "common/base/Result.h"

namespace ea {

class IMemory {
public:
    virtual ~IMemory() = default;
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;
};

}  // namespace ea
