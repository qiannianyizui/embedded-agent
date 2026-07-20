#include "BudgetTracker.h"
#include "common/io/Logger.h"
#include <algorithm>

namespace ea::budget {

BudgetTracker::BudgetTracker(std::shared_ptr<IProvider> inner, BudgetConfig config)
    : inner_(std::move(inner))
    , config_(std::move(config)) {}

std::string BudgetTracker::name() const {
    return "budget:" + inner_->name();
}

std::vector<std::string> BudgetTracker::list_models() const {
    return inner_->list_models();
}

Result<LLMResponse> BudgetTracker::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    const ChatOptions& opts) {
    auto result = inner_->chat(messages, tools, model, opts);
    if (result.ok()) {
        record_usage(result.value().usage, model);
    }
    return result;
}

Result<void> BudgetTracker::stream_chat(
    const std::vector<Message>& messages,
    const std::vector<ToolSpec>& tools,
    const std::string& model,
    std::function<void(const StreamChunk&)> on_chunk,
    const ChatOptions& opts) {
    std::string captured_model = model;
    auto wrapped_callback = [this, captured_model, on_chunk = std::move(on_chunk)](
        const StreamChunk& chunk) {
        if (chunk.type == StreamChunk::Type::Done && chunk.usage.has_value()) {
            record_usage(chunk.usage.value(), captured_model);
        }
        on_chunk(chunk);
    };

    return inner_->stream_chat(messages, tools, model, std::move(wrapped_callback), opts);
}

provider::ProviderCapabilities BudgetTracker::capabilities() const {
    return inner_->capabilities();
}

UsageSnapshot BudgetTracker::session_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_usage_;
}

CostSnapshot BudgetTracker::session_cost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_cost_;
}

UsageSnapshot BudgetTracker::global_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return global_usage_;
}

CostSnapshot BudgetTracker::global_cost() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return global_cost_;
}

void BudgetTracker::reset_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    session_usage_ = UsageSnapshot{};
    session_cost_ = CostSnapshot{};
}

void BudgetTracker::reset_global() {
    std::lock_guard<std::mutex> lock(mutex_);
    global_usage_ = UsageSnapshot{};
    global_cost_ = CostSnapshot{};
}

bool BudgetTracker::is_over_warn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_cost_.total() > config_.warn_cost_usd
        || session_usage_.input_tokens > config_.warn_input_tokens
        || session_usage_.output_tokens > config_.warn_output_tokens;
}

bool BudgetTracker::is_over_limit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.max_cost_usd > 0.0 && session_cost_.total() > config_.max_cost_usd) {
        return true;
    }
    if (config_.max_input_tokens > 0 && session_usage_.input_tokens > config_.max_input_tokens) {
        return true;
    }
    if (config_.max_output_tokens > 0 && session_usage_.output_tokens > config_.max_output_tokens) {
        return true;
    }
    return false;
}

void BudgetTracker::set_store(std::shared_ptr<IUsageStore> store) {
    store_ = std::move(store);
}

void BudgetTracker::set_session_id(const std::string& session_id) {
    session_id_ = session_id;
}

void BudgetTracker::flush() {
    // No-op: records are written immediately in record_usage()
}

void BudgetTracker::record_usage(const Usage& usage, const std::string& model) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Convert Usage -> UsageSnapshot
    UsageSnapshot snapshot;
    snapshot.input_tokens = usage.input_tokens;
    snapshot.output_tokens = usage.output_tokens;
    snapshot.cache_read_tokens = usage.cache_read_tokens;
    snapshot.cache_write_tokens = usage.cache_write_tokens;

    // Accumulate session and global counters
    session_usage_ += snapshot;
    global_usage_ += snapshot;

    // Calculate cost
    CostSnapshot cost = calculate_cost(snapshot, model);
    session_cost_ += cost;
    global_cost_ += cost;

    // Persist if store is set
    if (store_) {
        UsageRecord rec;
        rec.session_id = session_id_;
        rec.model = model;
        rec.input_tokens = usage.input_tokens;
        rec.output_tokens = usage.output_tokens;
        rec.cache_read_tokens = usage.cache_read_tokens;
        rec.cache_write_tokens = usage.cache_write_tokens;
        rec.cost_usd = cost.total();
        store_->record(rec);
    }

    // Check budget thresholds
    check_budget();
}

const ModelPricing* BudgetTracker::find_pricing(const std::string& model) const {
    for (const auto& mp : config_.pricing) {
        if (model.size() >= mp.model_id.size() &&
            model.compare(0, mp.model_id.size(), mp.model_id) == 0) {
            return &mp;
        }
    }
    return nullptr;
}

CostSnapshot BudgetTracker::calculate_cost(const UsageSnapshot& snapshot,
                                           const std::string& model) const {
    const ModelPricing* pricing = find_pricing(model);
    if (!pricing) {
        return CostSnapshot{};
    }
    return pricing->calculate(snapshot);
}

void BudgetTracker::check_budget() const {
    // Warning thresholds
    if (session_cost_.total() > config_.warn_cost_usd) {
        EA_WARN("Budget warn: session cost ${:.4f} exceeds warn threshold ${:.4f}",
                session_cost_.total(), config_.warn_cost_usd);
    }
    if (session_usage_.input_tokens > config_.warn_input_tokens) {
        EA_WARN("Budget warn: session input tokens {} exceeds warn threshold {}",
                session_usage_.input_tokens, config_.warn_input_tokens);
    }
    if (session_usage_.output_tokens > config_.warn_output_tokens) {
        EA_WARN("Budget warn: session output tokens {} exceeds warn threshold {}",
                session_usage_.output_tokens, config_.warn_output_tokens);
    }

    // Hard limit thresholds
    if (config_.max_cost_usd > 0.0 && session_cost_.total() > config_.max_cost_usd) {
        EA_ERROR("Budget limit: session cost ${:.4f} exceeds max ${:.4f}",
                 session_cost_.total(), config_.max_cost_usd);
    }
    if (config_.max_input_tokens > 0 && session_usage_.input_tokens > config_.max_input_tokens) {
        EA_ERROR("Budget limit: session input tokens {} exceeds max {}",
                 session_usage_.input_tokens, config_.max_input_tokens);
    }
    if (config_.max_output_tokens > 0 && session_usage_.output_tokens > config_.max_output_tokens) {
        EA_ERROR("Budget limit: session output tokens {} exceeds max {}",
                 session_usage_.output_tokens, config_.max_output_tokens);
    }
}

}  // namespace ea::budget
