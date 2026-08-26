// PermissionMode — shared permission state between AgentLoop, tools and the UI.
// Four modes, cycled with Shift+Tab:
//   Manual            — ask before every mutating tool call
//   Auto              — no approval, everything runs
//   AcceptEdits       — auto-accept file write/edit, ask for everything else
//   Plan              — read-only: mutating tools blocked, plan_exit handoff
#pragma once
#include <atomic>
#include <string>

namespace ea::agent {

enum class PermissionMode {
    Manual,
    AcceptEdits,
    Plan,
    BypassPermissions,  // displayed / persisted as "auto"
};

inline const char* permission_mode_name(PermissionMode m) {
    switch (m) {
        case PermissionMode::Manual: return "manual";
        case PermissionMode::AcceptEdits: return "acceptEdits";
        case PermissionMode::Plan: return "plan";
        case PermissionMode::BypassPermissions: return "auto";
    }
    return "manual";
}

inline PermissionMode permission_mode_from_name(const std::string& name) {
    if (name == "manual") return PermissionMode::Manual;
    if (name == "default") return PermissionMode::Manual;  // legacy alias
    if (name == "plan") return PermissionMode::Plan;
    if (name == "acceptEdits") return PermissionMode::AcceptEdits;
    if (name == "auto") return PermissionMode::BypassPermissions;
    if (name == "bypassPermissions" || name == "bypass")
        return PermissionMode::BypassPermissions;  // legacy alias
    return PermissionMode::Manual;
}

// Shared state between AgentLoop, tools and the UI (thread-safe).
struct PermissionState {
    std::atomic<PermissionMode> mode{PermissionMode::Manual};
    // Non-plan mode restored when leaving plan mode (Shift+Tab toggling).
    std::atomic<PermissionMode> previous{PermissionMode::Manual};
    // Set when plan_exit is approved; AgentLoop switches mode and continues
    // the same run with an "execute the plan" message.
    std::atomic<bool> exit_approved{false};
    std::string plan_file;
};

}  // namespace ea::agent