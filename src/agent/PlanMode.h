// PlanMode — shared state between AgentLoop, PlanExitTool and the UI
#pragma once
#include <atomic>
#include <string>

namespace ea::agent {

struct PlanModeState {
    std::atomic<bool> active{false};
    std::atomic<bool> exit_approved{false};
    std::string plan_file;
};

}  // namespace ea::agent
