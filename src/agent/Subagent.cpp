#include "Subagent.h"
#include "AgentLoop.h"
#include "log/Logger.h"

namespace ea::agent {

Subagent::Subagent(std::string id, SubagentConfig config,
                   IProvider* provider, tool::ToolRegistry* registry, IMemory* memory)
    : id_(std::move(id))
    , config_(std::move(config))
    , provider_(provider)
    , registry_(registry)
    , memory_(memory) {}

Result<std::string> Subagent::execute(const std::string& task) {
    if (running_.exchange(true)) {
        return Error::invalid_arg("Subagent '" + id_ + "' is already running");
    }

    EA_INFO("Subagent '{}' executing task: {}", id_, task.substr(0, 100));

    std::string output;
    AgentLoop loop(
        provider_, registry_, memory_,
        AgentLoop::Config{config_.max_iterations},
        [&](const std::string& text) { output = text; },
        nullptr,  // stream_fn — subagents don't stream
        nullptr,  // policy — subagents don't need approval flow
        nullptr,  // approval handler
        nullptr   // compressor
    );

    auto result = loop.run(task);

    running_ = false;

    if (!result.ok()) {
        EA_WARN("Subagent '{}' failed: {}", id_, result.error().message);
        return result.error();
    }

    EA_INFO("Subagent '{}' completed", id_);
    return output;
}

}  // namespace ea::agent
