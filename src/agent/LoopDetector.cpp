#include "LoopDetector.h"
#include <algorithm>

namespace ea::agent {

std::string LoopDetector::call_signature(const ToolCall& call) const {
    // Simple signature: tool_name + arguments as string
    return call.name + ":" + call.arguments.dump();
}

std::string LoopDetector::result_signature(const ToolResult& result) const {
    // Simple hash: first 64 chars of output + is_error flag
    std::string sig;
    sig += result.is_error ? "E" : "O";
    sig += result.output.substr(0, 64);
    return sig;
}

LoopAction LoopDetector::check(const ToolCall& call, const ToolResult& result) {
    CallRecord record;
    record.signature = call_signature(call);
    record.result_hash = result_signature(result);
    record.tool_name = call.name;

    // Check patterns before adding to history
    bool exact = is_exact_repeat(call);
    bool pong = is_ping_pong(call);
    bool noprogress = is_no_progress(call, result);

    // Add to history
    recent_calls_.push_back(std::move(record));
    tool_result_history_.push_back({call.name, result_signature(result)});

    // Keep history bounded
    while (recent_calls_.size() > 20) {
        recent_calls_.pop_front();
    }
    while (tool_result_history_.size() > 50) {
        tool_result_history_.pop_front();
    }

    // Escalation: Break > Block > Warn > Continue
    if (noprogress) return LoopAction::Break;
    if (exact) return LoopAction::Block;
    if (pong) return LoopAction::Warn;

    return LoopAction::Continue;
}

bool LoopDetector::is_exact_repeat(const ToolCall& call) const {
    if (recent_calls_.size() < static_cast<size_t>(exact_repeat_threshold - 1)) return false;

    std::string sig = call_signature(call);
    int count = 1;  // current call
    for (auto it = recent_calls_.rbegin();
         it != recent_calls_.rend() && it->signature == sig;
         ++it) {
        count++;
    }
    return count >= exact_repeat_threshold;
}

bool LoopDetector::is_ping_pong(const ToolCall& call) const {
    if (recent_calls_.size() < static_cast<size_t>(ping_pong_threshold - 1)) return false;

    // Check if we're alternating between two tools
    std::string current = call.name;
    int alternations = 0;

    for (auto it = recent_calls_.rbegin(); it != recent_calls_.rend(); ++it) {
        if (it->tool_name != current) {
            alternations++;
            current = it->tool_name;
        } else {
            break;  // consecutive same tool breaks the pattern
        }
    }

    return alternations >= ping_pong_threshold;
}

bool LoopDetector::is_no_progress(const ToolCall& call, const ToolResult& result) const {
    std::string res_sig = result_signature(result);
    int same_count = 0;

    for (auto it = tool_result_history_.rbegin(); it != tool_result_history_.rend(); ++it) {
        if (it->first != call.name) continue;  // skip different tools
        if (it->second == res_sig) {
            same_count++;
        } else {
            break;  // same tool but different result — stop
        }
    }

    // +1 for current call
    return (same_count + 1) >= no_progress_threshold;
}

void LoopDetector::reset() {
    recent_calls_.clear();
    tool_result_history_.clear();
}

}  // namespace ea::agent
