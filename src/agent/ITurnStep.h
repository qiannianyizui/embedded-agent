// ITurnStep — pluggable step interface for the agent loop
#pragma once
#include "TurnContext.h"
#include "base/Result.h"
#include <string>
#include <memory>
#include <vector>

namespace ea::agent {

class ITurnStep {
public:
    virtual ~ITurnStep() = default;
    virtual Result<void> execute(TurnContext& ctx) = 0;
    virtual std::string name() const = 0;
};

// Run a chain of steps in order, stopping on error or should_stop
inline Result<void> run_step_chain(TurnContext& ctx, const std::vector<std::unique_ptr<ITurnStep>>& steps) {
    for (const auto& step : steps) {
        if (ctx.interrupted) {
            return Error::timeout("agent loop interrupted");
        }
        if (ctx.should_stop) {
            return {};
        }
        auto result = step->execute(ctx);
        if (!result.ok()) {
            return result;
        }
    }
    return {};
}

}  // namespace ea::agent
