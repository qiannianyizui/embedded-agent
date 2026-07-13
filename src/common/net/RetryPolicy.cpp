#include "RetryPolicy.h"

namespace ea::net {

bool RetryPolicy::should_retry(int status) const {
    return status == 429 || (status >= 500 && status < 600);
}

std::chrono::milliseconds RetryPolicy::delay_for(int attempt) const {
    auto delay = base_delay;
    for (int i = 0; i < attempt; ++i) {
        delay = std::chrono::milliseconds(
            static_cast<long>(delay.count() * backoff_multiplier));
    }
    if (delay > max_delay) delay = max_delay;
    return delay;
}

}  // namespace ea::net
