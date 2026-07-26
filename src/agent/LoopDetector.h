// LoopDetector — detects 3 loop patterns in agent tool calls
// Patterns: exact repeat, ping-pong, no-progress
// Escalation: Continue → Warn → Block → Break
#pragma once
#include "common/base/Types.h"
#include <deque>
#include <string>

namespace ea::agent {

enum class LoopAction { Continue, Warn, Block, Break };

class LoopDetector {
public:
    LoopAction check(const ToolCall& call, const ToolResult& result);
    void reset();

    // Configuration
    int exact_repeat_threshold = 3;   // same tool+args N times consecutively
    int ping_pong_threshold = 4;      // two tools alternating for N cycles
    int no_progress_threshold = 5;    // same tool called N times with identical result

private:
    bool is_exact_repeat(const ToolCall& call) const;
    bool is_ping_pong(const ToolCall& call) const;
    bool is_no_progress(const ToolCall& call, const ToolResult& result) const;

    std::string call_signature(const ToolCall& call) const;
    std::string result_signature(const ToolResult& result) const;

    struct CallRecord {
        std::string signature;    // tool_name + args hash
        std::string result_hash;  // result output hash
        std::string tool_name;
    };

    std::deque<CallRecord> recent_calls_;
    std::deque<std::pair<std::string, std::string>> tool_result_history_;
};

}  // namespace ea::agent
