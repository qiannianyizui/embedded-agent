// HistoryPruneStep — trim messages exceeding max count
#pragma once
#include "agent/ITurnStep.h"

namespace ea::agent {

class HistoryPruneStep : public ITurnStep {
public:
    explicit HistoryPruneStep(int max_messages = 100) : max_messages_(max_messages) {}

    Result<void> execute(TurnContext& ctx) override {
        if (static_cast<int>(ctx.messages.size()) <= max_messages_) {
            return {};
        }

        // Keep first message (system) and last N messages
        // Remove middle messages, leave a breadcrumb
        int to_remove = static_cast<int>(ctx.messages.size()) - max_messages_;
        if (to_remove > 0 && ctx.messages.size() > 2) {
            auto begin = ctx.messages.begin() + 1;
            auto end = begin + std::min(to_remove, static_cast<int>(ctx.messages.size()) - 2);
            ctx.messages.erase(begin, end);

            // Insert breadcrumb
            Message breadcrumb;
            breadcrumb.role = Role::System;
            breadcrumb.content = "[Earlier conversation history pruned to fit context window]";
            ctx.messages.insert(ctx.messages.begin() + 1, std::move(breadcrumb));
        }

        return {};
    }

    std::string name() const override { return "history_prune"; }

private:
    int max_messages_;
};

}  // namespace ea::agent
