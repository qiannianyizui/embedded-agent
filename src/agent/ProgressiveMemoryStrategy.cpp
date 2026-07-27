// ProgressiveMemoryStrategy — active, incremental memory management
// Extract facts → summarize → inject across turns
#include "ProgressiveMemoryStrategy.h"
#include "log/Logger.h"
#include "agent/ContextCompressor.h"
#include <sstream>
#include <algorithm>

namespace ea::agent {

ProgressiveMemoryStrategy::ProgressiveMemoryStrategy(ProgressiveMemoryConfig config)
    : config_(std::move(config)) {}

std::string ProgressiveMemoryStrategy::get_extraction_prompt() const {
    if (!config_.fact_extraction_prompt.empty()) {
        return config_.fact_extraction_prompt;
    }
    return "Extract key facts from this conversation turn. Return each fact as a separate line starting with \"- \".\n"
           "Focus on: user preferences, decisions made, important entities, constraints, and outcomes.\n"
           "Omit: greetings, acknowledgments, and routine exchanges.\n\n"
           "User: {user_input}\n"
           "Assistant: {assistant_output}";
}

std::string ProgressiveMemoryStrategy::get_summarization_prompt() const {
    if (!config_.summarization_prompt.empty()) {
        return config_.summarization_prompt;
    }
    return "Summarize the following conversation segment concisely, preserving key facts, decisions, and outcomes.\n"
           "Omit greetings and repetitions. Focus on information that would be needed in future turns.";
}

void ProgressiveMemoryStrategy::extract_facts(MemoryStrategyContext& ctx) {
    if (!config_.enable_fact_extraction) return;
    if (!ctx.provider || !ctx.memory) return;

    // Build extraction prompt
    std::string prompt = get_extraction_prompt();
    // Replace placeholders
    size_t pos;
    while ((pos = prompt.find("{user_input}")) != std::string::npos) {
        prompt.replace(pos, 12, ctx.user_input);
    }
    while ((pos = prompt.find("{assistant_output}")) != std::string::npos) {
        prompt.replace(pos, 18, ctx.assistant_output);
    }

    // Call LLM
    std::vector<Message> req = {
        {Role::System, "You are a fact extraction assistant. Extract only factual information.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = ctx.provider->chat(req, {}, "", opts);
    if (!response.ok()) {
        EA_WARN("Fact extraction failed: {}", response.error().message);
        return;
    }

    // Parse response: each line starting with "- " is a fact
    std::istringstream stream(response.value().content);
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        // Trim whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.pop_back();
        }

        if (line.size() > 2 && line[0] == '-' && line[1] == ' ') {
            std::string fact = line.substr(2);
            if (!fact.empty()) {
                auto store_result = ctx.memory->store(fact, "long_term", config_.long_term_importance);
                if (!store_result.ok()) {
                    EA_WARN("Failed to store extracted fact: {}", store_result.error().message);
                } else {
                    count++;
                }
            }
        }
    }

    EA_DEBUG("Extracted {} facts from turn", count);
}

void ProgressiveMemoryStrategy::summarize_old_messages(MemoryStrategyContext& ctx) {
    if (!config_.enable_auto_summarize) return;
    if (!ctx.provider || !ctx.memory) return;

    size_t max_working = static_cast<size_t>(config_.working_turns) * 2;
    if (ctx.history.size() <= max_working) return;

    // Find the boundary: system messages at front + non-system messages to summarize
    size_t sys_end = 0;
    while (sys_end < ctx.history.size() && ctx.history[sys_end].role == Role::System) {
        ++sys_end;
    }

    // Messages to summarize: from sys_end to (history.size() - max_working)
    size_t summarize_end = ctx.history.size() - max_working;
    if (summarize_end <= sys_end) return;  // Nothing to summarize beyond system messages

    // Collect messages to summarize
    std::vector<Message> to_summarize;
    for (size_t i = sys_end; i < summarize_end; ++i) {
        to_summarize.push_back(ctx.history[i]);
    }

    if (to_summarize.empty()) return;

    // LLM summarization
    std::string sum_prompt = get_summarization_prompt();
    std::string conv_text;
    for (const auto& msg : to_summarize) {
        const char* role_name = "unknown";
        switch (msg.role) {
            case Role::System:    role_name = "System"; break;
            case Role::User:      role_name = "User"; break;
            case Role::Assistant: role_name = "Assistant"; break;
            case Role::Tool:      role_name = "Tool"; break;
        }
        conv_text += role_name;
        conv_text += ": ";
        conv_text += msg.content;
        conv_text += "\n\n";
    }

    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, sum_prompt + "\n\n" + conv_text,
         std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = ctx.provider->chat(req, {}, "", opts);

    std::string summary;
    if (response.ok()) {
        summary = response.value().content;
    } else {
        EA_WARN("Summarization failed, using simple truncation: {}", response.error().message);
        summary = "[Earlier conversation history pruned to fit context window]";
    }

    // Store summary in short-term memory
    auto store_result = ctx.memory->store(summary, "short_term", 5);
    if (!store_result.ok()) {
        EA_WARN("Failed to store summary: {}", store_result.error().message);
    }

    // Remove summarized messages from history (in-place)
    // Keep: [0..sys_end) system messages + [summarize_end..end) recent messages
    // Insert: [Summary] breadcrumb after system messages

    Message breadcrumb;
    breadcrumb.role = Role::System;
    breadcrumb.content = "[Conversation Summary]\n" + summary;

    std::vector<Message> new_history;
    new_history.reserve(sys_end + 1 + (ctx.history.size() - summarize_end));

    for (size_t i = 0; i < sys_end; ++i) {
        new_history.push_back(std::move(ctx.history[i]));
    }
    new_history.push_back(std::move(breadcrumb));
    for (size_t i = summarize_end; i < ctx.history.size(); ++i) {
        new_history.push_back(std::move(ctx.history[i]));
    }

    size_t old_count = ctx.history.size();
    ctx.history = std::move(new_history);

    EA_INFO("Summarized {} old messages into short-term memory ({} -> {} messages in history)",
            to_summarize.size(), old_count, ctx.history.size());
}

void ProgressiveMemoryStrategy::evict_short_term(IMemory* memory) {
    if (!memory) return;

    // List with generous limit to ensure we get all short_term entries
    auto list_result = memory->list(1000, 0);
    if (!list_result.ok()) {
        EA_WARN("Failed to list short-term memories for eviction: {}", list_result.error().message);
        return;
    }

    // Filter to short_term category only
    std::vector<MemoryEntry> short_term_entries;
    for (const auto& entry : list_result.value()) {
        if (entry.category == "short_term") {
            short_term_entries.push_back(entry);
        }
    }

    if (static_cast<int>(short_term_entries.size()) <= config_.short_term_max) return;

    // Sort by created_at ascending so oldest entries come first.
    // SqliteMemory::list() returns ORDER BY created_at DESC (newest first),
    // so we must reverse-sort to evict the oldest as the spec requires.
    // If created_at is empty (e.g. InMemoryBackend), the existing
    // insertion-order behavior is preserved (stable sort keeps relative order).
    std::stable_sort(short_term_entries.begin(), short_term_entries.end(),
        [](const MemoryEntry& a, const MemoryEntry& b) {
            return a.created_at < b.created_at;
        });

    // Evict oldest entries
    int to_evict = static_cast<int>(short_term_entries.size()) - config_.short_term_max;
    int evicted = 0;
    for (int i = 0; i < to_evict && i < static_cast<int>(short_term_entries.size()); ++i) {
        auto forget_result = memory->forget(short_term_entries[i].id);
        if (forget_result.ok() && forget_result.value()) {
            evicted++;
        }
    }

    if (evicted > 0) {
        EA_DEBUG("Evicted {} old short-term memory entries", evicted);
    }
}

std::string ProgressiveMemoryStrategy::build_memory_prompt(const std::vector<Message>& history,
                                                            IMemory* memory) {
    if (!memory) return {};

    std::ostringstream prompt;

    // 1. Query long-term memory using last user message
    std::string last_user_msg;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->role == Role::User) {
            last_user_msg = it->content;
            break;
        }
    }

    bool has_any = false;

    if (!last_user_msg.empty()) {
        auto lt_result = memory->recall(last_user_msg, 3);
        if (lt_result.ok() && !lt_result.value().empty()) {
            // Filter to long_term category
            std::vector<MemoryEntry> long_term;
            for (const auto& entry : lt_result.value()) {
                if (entry.category == "long_term") {
                    long_term.push_back(entry);
                }
            }
            if (!long_term.empty()) {
                has_any = true;
                prompt << "# Conversation Context\n## Key Facts\n";
                for (const auto& entry : long_term) {
                    prompt << "- " << entry.content << "\n";
                }
            }
        }
    }

    // 2. Query short-term memory
    auto st_result = memory->list(config_.short_term_max, 0);
    if (st_result.ok()) {
        std::vector<MemoryEntry> short_term;
        for (const auto& entry : st_result.value()) {
            if (entry.category == "short_term") {
                short_term.push_back(entry);
            }
        }
        if (!short_term.empty()) {
            if (!has_any) {
                prompt << "# Conversation Context\n";
                has_any = true;
            }
            prompt << "## Recent Summary\n";
            for (const auto& entry : short_term) {
                prompt << "- " << entry.content << "\n";
            }
        }
    }

    return has_any ? prompt.str() : std::string{};
}

void ProgressiveMemoryStrategy::on_turn_end(MemoryStrategyContext& ctx) {
    // Step 1: Extract key facts
    extract_facts(ctx);

    // Step 2: Summarize old messages into short-term
    summarize_old_messages(ctx);

    // Step 3: Evict excess short-term entries
    evict_short_term(ctx.memory);
}

}  // namespace ea::agent
