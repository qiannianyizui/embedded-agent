#pragma once
#include <string>
#include <vector>

namespace ea::budget {

struct UsageSnapshot {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;

    UsageSnapshot& operator+=(const UsageSnapshot& other) {
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
        cache_read_tokens += other.cache_read_tokens;
        cache_write_tokens += other.cache_write_tokens;
        return *this;
    }

    UsageSnapshot operator+(const UsageSnapshot& other) const {
        UsageSnapshot result = *this;
        result += other;
        return result;
    }

    int total_tokens() const {
        return input_tokens + output_tokens + cache_read_tokens + cache_write_tokens;
    }
};

struct CostSnapshot {
    double input_cost = 0.0;
    double output_cost = 0.0;
    double cache_read_cost = 0.0;
    double cache_write_cost = 0.0;

    CostSnapshot& operator+=(const CostSnapshot& other) {
        input_cost += other.input_cost;
        output_cost += other.output_cost;
        cache_read_cost += other.cache_read_cost;
        cache_write_cost += other.cache_write_cost;
        return *this;
    }

    double total() const {
        return input_cost + output_cost + cache_read_cost + cache_write_cost;
    }
};

struct ModelPricing {
    std::string model_id;
    double input_per_mtok = 0.0;
    double output_per_mtok = 0.0;
    double cache_read_per_mtok = 0.0;
    double cache_write_per_mtok = 0.0;

    CostSnapshot calculate(const UsageSnapshot& usage) const {
        CostSnapshot cost;
        cost.input_cost = usage.input_tokens * input_per_mtok / 1000000.0;
        cost.output_cost = usage.output_tokens * output_per_mtok / 1000000.0;
        cost.cache_read_cost = usage.cache_read_tokens * cache_read_per_mtok / 1000000.0;
        cost.cache_write_cost = usage.cache_write_tokens * cache_write_per_mtok / 1000000.0;
        return cost;
    }
};

struct BudgetConfig {
    std::string path;
    int warn_input_tokens = 100000;
    int warn_output_tokens = 50000;
    double warn_cost_usd = 1.0;
    int max_input_tokens = 0;
    int max_output_tokens = 0;
    double max_cost_usd = 0.0;
    std::vector<ModelPricing> pricing;
};

}  // namespace ea::budget
