#pragma once
#include "core/Types.h"
#include "common/base/Result.h"

namespace ea {

class IMemory {
public:
    virtual ~IMemory() = default;

    // Existing CRUD
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;

    // Lifecycle hooks (default no-op implementations)
    virtual Result<void> open() { return {}; }
    virtual Result<void> close() { return {}; }
    virtual std::string system_prompt_block() const { return {}; }

    // Turn-level hooks
    virtual void on_turn_start(const std::string& user_input) { (void)user_input; }
    virtual void on_turn_end(const std::string& assistant_output) { (void)assistant_output; }
    virtual void on_pre_compress() {}
};

}  // namespace ea
