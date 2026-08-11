#include "ContextCompressor.h"
#include "log/Logger.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace ea::agent {

namespace {

constexpr int kDefaultContextLength = 128000;
constexpr int kMinContextFloor = 64000;
constexpr int kMaxSummaryCap = 12000;
constexpr int kToolPruneChars = 200;
constexpr int kToolArgTruncateChars = 500;
constexpr int kSummaryMaxChars = 6000;

int weighted_chars(const std::string& s) {
    int weight = 0;
    for (unsigned char c : s) {
        weight += (c >= 0xC0) ? 2 : 1;
    }
    return weight;
}

std::string truncate_middle(const std::string& s, size_t max_chars,
                            size_t head_chars = 4000, size_t tail_chars = 1500) {
    if (s.size() <= max_chars) return s;
    if (head_chars + tail_chars >= s.size()) return s;
    return s.substr(0, head_chars) + "\n...[truncated]...\n"
         + s.substr(s.size() - tail_chars);
}

std::string truncate_args(const std::string& args) {
    if (args.size() <= kToolArgTruncateChars) return args;
    return args.substr(0, kToolArgTruncateChars) + "...[truncated]";
}

std::string tool_one_liner(const std::string& name, const std::string& args,
                           const std::string& content) {
    std::ostringstream oss;
    oss << '[' << (name.empty() ? "tool" : name) << ']';
    std::string compact_args = args;
    // Keep only the first non-whitespace run of the args for the one-liner.
    size_t start = compact_args.find_first_not_of(" \t\r\n{");
    if (start != std::string::npos) {
        size_t end = compact_args.find_first_of(",}\n", start);
        if (end != std::string::npos) compact_args = compact_args.substr(start, end - start);
    }
    if (!compact_args.empty()) oss << ' ' << compact_args;
    oss << " (" << content.size() << " chars)";
    return oss.str();
}

}  // namespace

ContextCompressor::ContextCompressor(IProvider* provider, CompressionConfig config)
    : provider_(provider), config_(std::move(config)) {
    context_length_ = resolve_context_length();
    recompute_budgets();
}

int ContextCompressor::resolve_context_length() const {
    if (config_.context_length > 0) return config_.context_length;
    return kDefaultContextLength;
}

void ContextCompressor::recompute_budgets() {
    int effective = context_length_;
    double pct = std::clamp(config_.threshold_percent, 0.05, 0.95);
    int pct_value = static_cast<int>(effective * pct);
    int floored = std::max(pct_value, kMinContextFloor);
    if (floored >= effective) {
        threshold_tokens_ = std::max(1, std::min(
            static_cast<int>(effective * 0.85), effective - 1));
    } else {
        threshold_tokens_ = floored;
    }

    double ratio = std::clamp(config_.target_ratio, 0.10, 0.80);
    tail_token_budget_ = static_cast<int>(threshold_tokens_ * ratio);

    if (config_.max_summary_tokens > 0) {
        max_summary_tokens_ = config_.max_summary_tokens;
    } else {
        max_summary_tokens_ = std::min(static_cast<int>(context_length_ * 0.05),
                                       kMaxSummaryCap);
    }
}

int ContextCompressor::estimate_tokens(const Message& msg) {
    int content_weight = weighted_chars(msg.content);
    if (content_weight == 0 && !msg.name && !msg.tool_calls && !msg.tool_call_id) {
        return 0;
    }
    int tokens = (content_weight + 3) / 4;
    if (msg.name) tokens += (static_cast<int>(msg.name->size()) + 3) / 4;
    if (msg.tool_calls) {
        for (const auto& tc : *msg.tool_calls) {
            tokens += (static_cast<int>(tc.name.size()) + 3) / 4;
            tokens += (static_cast<int>(tc.arguments.size()) + 3) / 4;
        }
    }
    if (msg.tool_call_id) tokens += (static_cast<int>(msg.tool_call_id->size()) + 3) / 4;
    return std::max(0, tokens);
}

int ContextCompressor::estimate_tokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) total += estimate_tokens(msg);
    return total;
}

bool ContextCompressor::should_compress(const std::vector<Message>& messages) const {
    if (!config_.enable) return false;
    if (ineffective_count_ >= 2) return false;
    int tokens = last_prompt_tokens_ > 0 ? last_prompt_tokens_
                                         : estimate_tokens(messages);
    return tokens >= threshold_tokens_;
}

void ContextCompressor::update_from_response(int prompt_tokens) {
    last_prompt_tokens_ = prompt_tokens;
}

std::vector<Message> ContextCompressor::prune_old_tool_results(
        const std::vector<Message>& messages) const {
    if (messages.empty()) return messages;

    int head_end = protect_head_size(messages);
    int cut = find_tail_cut(messages, head_end);
    std::vector<Message> result = messages;
    std::unordered_map<std::string, int> seen;

    for (int i = 0; i < cut && i < static_cast<int>(result.size()); ++i) {
        auto& msg = result[static_cast<size_t>(i)];
        if (msg.role == Role::Tool) {
            if (msg.content.size() < kToolPruneChars) continue;
            std::string key = msg.content.substr(0, 256);
            auto it = seen.find(key);
            if (it != seen.end()) {
                msg.content = "[Duplicate tool output — same content as a more recent call]";
                continue;
            }
            seen.emplace(key, i);
            std::string name = msg.name ? *msg.name : "tool";
            std::string args;
            if (msg.tool_call_id && !msg.tool_call_id->empty()) {
                // Keep a compact reference to the call; args live on the
                // assistant message that triggered it.
                args = "call=" + *msg.tool_call_id;
            }
            msg.content = tool_one_liner(name, args, msg.content);
        } else if (msg.role == Role::Assistant && msg.tool_calls) {
            for (auto& tc : *msg.tool_calls) {
                if (tc.arguments.size() > kToolArgTruncateChars) {
                    tc.arguments = truncate_args(tc.arguments);
                }
            }
        }
    }
    return result;
}

int ContextCompressor::protect_head_size(const std::vector<Message>& messages) const {
    if (messages.empty()) return 0;
    int head = (messages[0].role == Role::System) ? 1 : 0;
    if (compression_count_ == 0 && previous_summary_.empty()) {
        head = std::min(head + config_.protect_first_n,
                        static_cast<int>(messages.size()));
    }
    while (head < static_cast<int>(messages.size()) &&
           messages[static_cast<size_t>(head)].role == Role::Tool) {
        ++head;
    }
    return head;
}

int ContextCompressor::find_tail_cut(const std::vector<Message>& messages,
                                     int head_end) const {
    int n = static_cast<int>(messages.size());
    if (n <= head_end + 1) return n;

    int available = std::max(0, n - head_end - 1);
    int min_tail = std::min(config_.protect_last_n, std::max(3, available));
    min_tail = std::min(min_tail, available);

    int soft_ceiling = std::max(1, tail_token_budget_ * 3 / 2);
    int accumulated = 0;
    int cut = n;

    for (int i = n - 1; i >= head_end; --i) {
        int t = estimate_tokens(messages[static_cast<size_t>(i)]) + 10;
        if (accumulated + t > soft_ceiling && (n - i) >= min_tail) break;
        accumulated += t;
        cut = i;
    }

    int fallback_cut = n - min_tail;
    cut = std::min(cut, fallback_cut);
    if (cut <= head_end) cut = std::max(fallback_cut, head_end + 1);

    // Always keep the most recent user/assistant message in the tail.
    for (int i = n - 1; i >= head_end; --i) {
        auto role = messages[static_cast<size_t>(i)].role;
        if (role == Role::User || role == Role::Assistant) {
            cut = std::min(cut, i);
            break;
        }
    }
    return std::max(cut, head_end + 1);
}

Result<std::string> ContextCompressor::summarize(
        const std::vector<Message>& middle, const std::string& focus_topic) {
    if (!provider_) {
        return Error::invalid_arg("No provider for summarization");
    }

    std::ostringstream prompt;
    prompt << "Summarize the following conversation concisely, preserving key "
              "facts, decisions, and outcomes. Omit greetings and repetitions.\n";
    if (!focus_topic.empty()) {
        prompt << "Focus on: " << focus_topic << "\n";
    }
    if (!previous_summary_.empty()) {
        prompt << "Previous summary (update it with the new turns below):\n"
               << previous_summary_ << "\n\n";
    }

    for (const auto& msg : middle) {
        const char* role = "unknown";
        switch (msg.role) {
            case Role::System:    role = "System"; break;
            case Role::User:      role = "User"; break;
            case Role::Assistant: role = "Assistant"; break;
            case Role::Tool:      role = "Tool"; break;
        }
        prompt << role << ": "
               << truncate_middle(msg.content, kSummaryMaxChars) << "\n\n";
        if (msg.tool_calls) {
            for (const auto& tc : *msg.tool_calls) {
                prompt << "  tool_call " << tc.name << '('
                       << truncate_middle(tc.arguments, 1500, 1200, 0) << ")\n";
            }
        }
    }

    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt.str(), std::nullopt, std::nullopt, std::nullopt},
    };

    ChatOptions opts;
    auto response = provider_->chat(req, {}, "", opts);
    if (!response.ok()) {
        EA_WARN("Summarization failed: {}", response.error().message);
        return response.error();
    }
    previous_summary_ = response.value().content;
    return previous_summary_;
}

std::string ContextCompressor::static_fallback_summary(
        const std::vector<Message>& middle) const {
    std::ostringstream oss;
    oss << "[Summary unavailable — deterministic fallback]\n";
    int shown = 0;
    for (auto it = middle.rbegin(); it != middle.rend() && shown < 8; ++it, ++shown) {
        const char* role = "?";
        switch (it->role) {
            case Role::User:      role = "user"; break;
            case Role::Assistant: role = "assistant"; break;
            case Role::Tool:      role = "tool"; break;
            case Role::System:    role = "system"; break;
        }
        std::string text = it->content;
        if (text.size() > 300) text = text.substr(0, 300);
        oss << role << ": " << text << "\n";
    }
    return oss.str();
}

Result<std::vector<Message>> ContextCompressor::compress(
        const std::vector<Message>& messages, const std::string& focus_topic) {
    last_compressed_ = false;
    if (!config_.enable) return messages;

    int tokens = estimate_tokens(messages);
    if (tokens <= threshold_tokens_) return messages;

    auto pruned = prune_old_tool_results(messages);
    int head_end = protect_head_size(pruned);
    int tail_start = find_tail_cut(pruned, head_end);

    if (head_end >= tail_start) {
        last_savings_pct_ = 0;
        return pruned;
    }

    std::vector<Message> middle(pruned.begin() + head_end,
                                pruned.begin() + tail_start);
    auto summary_result = summarize(middle, focus_topic);
    std::string summary;
    if (!summary_result.ok()) {
        if (config_.abort_on_summary_failure) {
            EA_WARN("Compression aborted: {}", summary_result.error().message);
            return messages;
        }
        EA_WARN("Summary failed, using static fallback: {}",
                summary_result.error().message);
        summary = static_fallback_summary(middle);
    } else {
        summary = summary_result.value();
    }

    std::vector<Message> compressed;
    compressed.reserve(head_end + 1 + (pruned.size() - tail_start));
    for (int i = 0; i < head_end; ++i) {
        compressed.push_back(pruned[static_cast<size_t>(i)]);
    }

    Role last_head_role = head_end > 0
        ? pruned[static_cast<size_t>(head_end - 1)].role : Role::User;
    Role first_tail_role = tail_start < static_cast<int>(pruned.size())
        ? pruned[static_cast<size_t>(tail_start)].role : Role::User;
    Role summary_role = (last_head_role == Role::System ||
                         last_head_role == Role::Assistant ||
                         last_head_role == Role::Tool)
        ? Role::User : Role::Assistant;
    if (summary_role == first_tail_role) {
        Role flipped = summary_role == Role::User ? Role::Assistant : Role::User;
        if (flipped != last_head_role) summary_role = flipped;
    }

    Message summary_msg;
    summary_msg.role = summary_role;
    summary_msg.content = "[Conversation Summary]\n" + summary
        + "\n\n[End of summary — respond to the message below]";
    compressed.push_back(std::move(summary_msg));

    for (int i = tail_start; i < static_cast<int>(pruned.size()); ++i) {
        compressed.push_back(pruned[static_cast<size_t>(i)]);
    }

    ++compression_count_;
    last_compressed_ = true;
    int new_tokens = estimate_tokens(compressed);
    last_savings_pct_ = tokens > 0 ? (100 * (tokens - new_tokens) / tokens) : 0;
    if (last_savings_pct_ < 10) {
        ++ineffective_count_;
    } else {
        ineffective_count_ = 0;
    }

    EA_INFO("Context compressed: {} tokens -> {} tokens ({}% reduction)",
            tokens, new_tokens, last_savings_pct_);
    return compressed;
}

}  // namespace ea::agent
