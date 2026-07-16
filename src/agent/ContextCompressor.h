// src/agent/ContextCompressor.h
#pragma once
#include "core/IProvider.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <vector>
#include <string>

namespace ea::agent {

struct CompressionConfig {
    int max_tokens = 8000;          // Token threshold to trigger compression
    int keep_recent_turns = 4;      // Keep last N turns (1 turn = 1 user + 1 assistant)
    bool enable = true;             // Enable/disable compression
};

class ContextCompressor {
public:
    explicit ContextCompressor(IProvider* provider, CompressionConfig config);

    // Compress message list if over threshold, returns compressed copy
    Result<std::vector<Message>> compress(const std::vector<Message>& messages);

    // Estimate token count (heuristic: 4 chars ≈ 1 token for ASCII, 2 chars ≈ 1 token for CJK)
    static int estimate_tokens(const std::vector<Message>& messages);

    // Estimate tokens for a single message
    static int estimate_tokens(const Message& msg);

    const CompressionConfig& config() const { return config_; }

private:
    // Summarize a range of messages using LLM
    Result<std::string> summarize(const std::vector<Message>& messages);

    // Split messages into head (system), middle (to compress), tail (recent)
    struct SplitResult {
        std::vector<Message> head;
        std::vector<Message> middle;
        std::vector<Message> tail;
    };
    SplitResult split_messages(const std::vector<Message>& messages) const;

    IProvider* provider_;
    CompressionConfig config_;
};

}  // namespace ea::agent
