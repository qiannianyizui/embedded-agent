#pragma once
#include "Types.h"
#include "common/base/Result.h"
#include <string>
#include <vector>

namespace ea::budget {

struct UsageRecord {
    std::string id;
    std::string session_id;
    std::string model;
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;
    double cost_usd = 0.0;
    std::string timestamp;
};

class IUsageStore {
public:
    virtual ~IUsageStore() = default;
    virtual Result<std::string> record(const UsageRecord& rec) = 0;
    virtual Result<std::vector<UsageRecord>> query(
        const std::string& session_id = "",
        int limit = 100, int offset = 0) = 0;
    virtual Result<UsageSnapshot> total_usage(
        const std::string& session_id = "") = 0;
    virtual Result<CostSnapshot> total_cost(
        const std::string& session_id = "") = 0;
    virtual Result<bool> clear(
        const std::string& session_id = "") = 0;
    virtual Result<void> open() = 0;
    virtual Result<void> close() = 0;
};

}  // namespace ea::budget
