#pragma once
#include "provider/IProvider.h"
#include "base/Types.h"
#include "base/Result.h"
#include <vector>
#include <string>

namespace ea::agent {

struct CompressionConfig {
    bool enable = true;
    int context_length = 0;            // 0 = default (128000)
    double threshold_percent = 0.50;   // Compress at this % of the context window
    double target_ratio = 0.20;        // Fraction of threshold kept as recent tail
    int protect_first_n = 3;           // Non-system head messages preserved verbatim
    int protect_last_n = 20;           // Minimum recent messages preserved in tail
    int max_summary_tokens = 0;        // 0 = auto (5% of context, capped)
    bool abort_on_summary_failure = false;
    bool in_place = true;
};

class ContextCompressor {
public:
    explicit ContextCompressor(IProvider* provider, CompressionConfig config = {});

    // Compress if the message list exceeds the token threshold. Returns a
    // compressed copy; the caller decides whether to replace live context.
    Result<std::vector<Message>> compress(const std::vector<Message>& messages,
                                          const std::string& focus_topic = {});

    // Quick preflight check using the rough token estimate.
    bool should_compress(const std::vector<Message>& messages) const;

    // Feed real prompt_tokens from the provider response back into the
    // trigger decision (Hermes-style usage feedback).
    void update_from_response(int prompt_tokens);

    static int estimate_tokens(const std::vector<Message>& messages);
    static int estimate_tokens(const Message& msg);

    int context_length() const { return context_length_; }
    int threshold_tokens() const { return threshold_tokens_; }
    int tail_token_budget() const { return tail_token_budget_; }
    int compression_count() const { return compression_count_; }
    bool compressed_last_call() const { return last_compressed_; }
    int last_compression_savings_pct() const { return last_savings_pct_; }
    const std::string& previous_summary() const { return previous_summary_; }

    const CompressionConfig& config() const { return config_; }

private:
    int resolve_context_length() const;
    void recompute_budgets();

    std::vector<Message> prune_old_tool_results(const std::vector<Message>& messages) const;
    int protect_head_size(const std::vector<Message>& messages) const;
    int find_tail_cut(const std::vector<Message>& messages, int head_end) const;

    Result<std::string> summarize(const std::vector<Message>& middle,
                                  const std::string& focus_topic);
    std::string static_fallback_summary(const std::vector<Message>& middle) const;

    IProvider* provider_;
    CompressionConfig config_;
    int context_length_ = 128000;
    int threshold_tokens_ = 0;
    int tail_token_budget_ = 0;
    int max_summary_tokens_ = 0;

    int last_prompt_tokens_ = 0;
    int compression_count_ = 0;
    int ineffective_count_ = 0;
    int last_savings_pct_ = 0;
    bool last_compressed_ = false;
    std::string previous_summary_;
};

}  // namespace ea::agent
