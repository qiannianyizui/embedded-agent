#pragma once
#include "core/IProvider.h"
#include "Types.h"
#include "IUsageStore.h"
#include <memory>
#include <mutex>
#include <string>

namespace ea::budget {

class BudgetTracker : public IProvider {
public:
    BudgetTracker(std::shared_ptr<IProvider> inner, BudgetConfig config);

    // IProvider overrides
    std::string name() const override;
    std::vector<std::string> list_models() const override;
    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) override;
    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) override;
    provider::ProviderCapabilities capabilities() const override;

    // Query
    UsageSnapshot session_usage() const;
    CostSnapshot session_cost() const;
    UsageSnapshot global_usage() const;
    CostSnapshot global_cost() const;

    // Reset
    void reset_session();
    void reset_global();

    // Budget check
    bool is_over_warn() const;
    bool is_over_limit() const;

    // Persistence
    void set_store(std::shared_ptr<IUsageStore> store);
    void set_session_id(const std::string& session_id);
    void flush();

private:
    void record_usage(const Usage& usage, const std::string& model);
    const ModelPricing* find_pricing(const std::string& model) const;
    CostSnapshot calculate_cost(const UsageSnapshot& snapshot, const std::string& model) const;
    void check_budget() const;

    std::shared_ptr<IProvider> inner_;
    BudgetConfig config_;
    std::shared_ptr<IUsageStore> store_;
    std::string session_id_;

    mutable std::mutex mutex_;
    UsageSnapshot session_usage_;
    CostSnapshot session_cost_;
    UsageSnapshot global_usage_;
    CostSnapshot global_cost_;
};

}  // namespace ea::budget
