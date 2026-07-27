// src/agent/ContextCompressor.cpp
#include "ContextCompressor.h"
#include "log/Logger.h"

namespace ea::agent {

ContextCompressor::ContextCompressor(IProvider* provider, CompressionConfig config)
    : provider_(provider), config_(std::move(config)) {}

int ContextCompressor::estimate_tokens(const Message& msg) {
    // Heuristic: 4 chars ≈ 1 token (ASCII), 2 chars ≈ 1 token (CJK/multibyte)
    int weighted_chars = 0;
    for (unsigned char c : msg.content) {
        if (c >= 0xC0) {
            weighted_chars += 2;  // CJK/multibyte start byte
        } else {
            weighted_chars += 1;  // ASCII
        }
    }
    return (weighted_chars + 3) / 4;  // Round up
}

int ContextCompressor::estimate_tokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        total += estimate_tokens(msg);
    }
    return total;
}

ContextCompressor::SplitResult
ContextCompressor::split_messages(const std::vector<Message>& messages) const {
    SplitResult result;

    if (messages.empty()) return result;

    // Head: system message(s) at the beginning
    size_t i = 0;
    while (i < messages.size() && messages[i].role == Role::System) {
        result.head.push_back(messages[i]);
        ++i;
    }

    // Tail: last keep_recent_turns * 2 messages (user + assistant per turn)
    size_t tail_count = static_cast<size_t>(config_.keep_recent_turns) * 2;
    size_t tail_start = messages.size();
    if (tail_start > tail_count + i) {
        tail_start = messages.size() - tail_count;
    } else {
        tail_start = i;  // Not enough messages for tail, keep all from i
    }

    // Middle: everything between head and tail
    for (size_t j = i; j < tail_start; ++j) {
        result.middle.push_back(messages[j]);
    }

    // Tail
    for (size_t j = tail_start; j < messages.size(); ++j) {
        result.tail.push_back(messages[j]);
    }

    return result;
}

Result<std::string> ContextCompressor::summarize(const std::vector<Message>& messages) {
    if (!provider_) {
        return Error::invalid_arg("No provider for summarization");
    }

    std::string prompt = "Summarize the following conversation concisely, "
        "preserving key facts, decisions, and outcomes. "
        "Omit greetings and repetitions.\n\n";

    for (const auto& msg : messages) {
        const char* role_name = "unknown";
        switch (msg.role) {
            case Role::System:    role_name = "System"; break;
            case Role::User:      role_name = "User"; break;
            case Role::Assistant: role_name = "Assistant"; break;
            case Role::Tool:      role_name = "Tool"; break;
        }
        prompt += role_name;
        prompt += ": ";
        prompt += msg.content;
        prompt += "\n\n";
    }

    // Call LLM with summarization request
    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = provider_->chat(req, {}, "", opts);
    if (!response.ok()) {
        EA_WARN("Summarization failed: {}", response.error().message);
        return response.error();
    }

    EA_DEBUG("Summarized {} messages into {} tokens",
             messages.size(), estimate_tokens(Message{Role::Assistant, response.value().content,
             std::nullopt, std::nullopt, std::nullopt}));
    return response.value().content;
}

Result<std::vector<Message>> ContextCompressor::compress(const std::vector<Message>& messages) {
    if (!config_.enable) {
        return messages;  // Compression disabled
    }

    int tokens = estimate_tokens(messages);
    if (tokens <= config_.max_tokens) {
        return messages;  // Under threshold, no compression needed
    }

    EA_INFO("Context compression triggered: {} tokens > {} max_tokens",
            tokens, config_.max_tokens);

    auto split = split_messages(messages);

    if (split.middle.empty()) {
        // No middle to compress
        return messages;
    }

    // Try LLM summarization
    auto summary_result = summarize(split.middle);
    if (summary_result.ok()) {
        // Build compressed message list: head + summary + tail
        std::vector<Message> compressed;
        compressed.reserve(split.head.size() + 1 + split.tail.size());

        for (const auto& msg : split.head) {
            compressed.push_back(msg);
        }

        // Insert summary as a system message
        compressed.push_back({
            Role::System,
            "[Conversation Summary]\n" + summary_result.value(),
            std::nullopt, std::nullopt, std::nullopt
        });

        for (const auto& msg : split.tail) {
            compressed.push_back(msg);
        }

        int new_tokens = estimate_tokens(compressed);
        EA_INFO("Context compressed: {} tokens → {} tokens ({}% reduction)",
                tokens, new_tokens,
                (100 * (tokens - new_tokens) / tokens));

        return compressed;
    }

    // Fallback: simple truncation (keep head + tail, drop middle)
    EA_WARN("Summarization failed, falling back to simple truncation");
    std::vector<Message> truncated;
    truncated.reserve(split.head.size() + 1 + split.tail.size());

    for (const auto& msg : split.head) {
        truncated.push_back(msg);
    }

    truncated.push_back({
        Role::System,
        "[Earlier conversation history pruned to fit context window]",
        std::nullopt, std::nullopt, std::nullopt
    });

    for (const auto& msg : split.tail) {
        truncated.push_back(msg);
    }

    return truncated;
}

}  // namespace ea::agent
