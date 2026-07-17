// Subagent — independent AgentLoop execution for delegated tasks
#pragma once
#include "SubagentConfig.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <atomic>
#include <string>

namespace ea::agent {

class Subagent {
public:
    Subagent(std::string id, SubagentConfig config,
             IProvider* provider, tool::ToolRegistry* registry, IMemory* memory);

    Result<std::string> execute(const std::string& task);

    const std::string& id() const { return id_; }
    const SubagentConfig& config() const { return config_; }
    bool is_running() const { return running_.load(); }

private:
    std::string id_;
    SubagentConfig config_;
    IProvider* provider_;
    tool::ToolRegistry* registry_;
    IMemory* memory_;
    std::atomic<bool> running_{false};
};

}  // namespace ea::agent
