#pragma once
#include <chrono>

namespace ea::net {

class RetryPolicy {
public:
    int max_retries = 3;
    std::chrono::milliseconds base_delay{1000};
    double backoff_multiplier = 2.0;
    std::chrono::milliseconds max_delay{30000};

    bool should_retry(int status) const;
    std::chrono::milliseconds delay_for(int attempt) const;
};

}  // namespace ea::net
