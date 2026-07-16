#pragma once
#include "common/base/Result.h"
#include <string>
#include <vector>
#include <mutex>

namespace ea::provider {

struct CredentialSlot {
    std::string api_key;
    std::string model;
    bool healthy = true;
    int consecutive_errors = 0;
};

class CredentialPool {
public:
    void add(CredentialSlot slot);

    Result<CredentialSlot> acquire();
    void release(const CredentialSlot& slot, bool success);

    size_t size() const;
    size_t healthy_count() const;

private:
    mutable std::mutex mutex_;
    std::vector<CredentialSlot> slots_;
    size_t current_index_ = 0;
};

}  // namespace ea::provider
