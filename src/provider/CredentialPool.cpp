#include "CredentialPool.h"

namespace ea::provider {

void CredentialPool::add(CredentialSlot slot) {
    std::lock_guard lock(mutex_);
    slots_.push_back(std::move(slot));
}

size_t CredentialPool::size() const {
    std::lock_guard lock(mutex_);
    return slots_.size();
}

size_t CredentialPool::healthy_count() const {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& s : slots_) {
        if (s.healthy) ++count;
    }
    return count;
}

Result<CredentialSlot> CredentialPool::acquire() {
    std::lock_guard lock(mutex_);
    if (slots_.empty()) {
        return Error::not_found("No credentials configured");
    }

    size_t start = current_index_;
    do {
        if (slots_[current_index_].healthy) {
            auto slot = slots_[current_index_];
            current_index_ = (current_index_ + 1) % slots_.size();
            return slot;
        }
        current_index_ = (current_index_ + 1) % slots_.size();
    } while (current_index_ != start);

    return Error::not_found("No healthy credentials available");
}

void CredentialPool::release(const CredentialSlot& slot, bool success) {
    std::lock_guard lock(mutex_);
    for (auto& s : slots_) {
        if (s.api_key == slot.api_key && s.model == slot.model) {
            if (success) {
                s.consecutive_errors = 0;
                s.healthy = true;
            } else {
                s.consecutive_errors++;
                if (s.consecutive_errors >= 3) {
                    s.healthy = false;
                }
            }
            return;
        }
    }
}

}  // namespace ea::provider
