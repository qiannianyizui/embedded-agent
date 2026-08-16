// ProgressiveMemoryStrategy — memory prompt injection across turns.
// Fact extraction is handled by the background MemoryExtractor.
#include "ProgressiveMemoryStrategy.h"
#include "log/Logger.h"
#include "agent/ContextCompressor.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ea::agent {

ProgressiveMemoryStrategy::ProgressiveMemoryStrategy(ProgressiveMemoryConfig config)
    : config_(std::move(config)) {}

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
        // Use search_facts() for holographic memory (includes trust + entity scoring)
        if (memory->is_holographic()) {
            auto lt_result = memory->search_facts(last_user_msg, "long_term", 0.3, 3);
            if (lt_result.ok() && !lt_result.value().empty()) {
                has_any = true;
                prompt << "# Conversation Context\n## Key Facts\n";
                for (const auto& fe : lt_result.value()) {
                    prompt << "- " << fe.content << " [trust: "
                           << std::fixed << std::setprecision(1) << fe.trust_score << "]\n";
                }
            }
        } else {
            auto lt_result = memory->recall(last_user_msg, 3);
            if (lt_result.ok() && !lt_result.value().empty()) {
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

void ProgressiveMemoryStrategy::on_turn_end(MemoryStrategyContext& /*ctx*/) {
    // No-op: fact extraction runs in the background MemoryExtractor.
}

}  // namespace ea::agent
