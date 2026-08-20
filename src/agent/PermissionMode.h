// PermissionMode — shared permission state between AgentLoop, tools and the UI.
// Mirrors Claude Code's four permission modes:
//   Default          — ask before every mutating tool call
//   AcceptEdits      — auto-accept file write/edit, ask for everything else
//   Plan             — read-only: mutating tools blocked, plan_exit handoff
//   BypassPermissions— no approval, everything runs
#pragma once
#include <atomic>
#include <string>

namespace ea::agent {

enum class PermissionMode {
    Default,
    AcceptEdits,
    Plan,
    BypassPermissions,
};

inline const char* permission_mode_name(PermissionMode m) {
    switch (m) {
        case PermissionMode::Default: return "default";
        case PermissionMode::AcceptEdits: return "acceptEdits";
        case PermissionMode::Plan: return "plan";
        case PermissionMode::BypassPermissions: return "bypassPermissions";
    }
    return "default";
}

inline PermissionMode permission_mode_from_name(const std::string& name) {
    if (name == "plan") return PermissionMode::Plan;
    if (name == "acceptEdits") return PermissionMode::AcceptEdits;
    if (name == "bypassPermissions") return PermissionMode::BypassPermissions;
    return PermissionMode::Default;
}

// Shared state between AgentLoop, tools and the UI (thread-safe).
struct PermissionState {
    std::atomic<PermissionMode> mode{PermissionMode::Default};
    // Non-plan mode restored when leaving plan mode (Shift+Tab toggling).
    std::atomic<PermissionMode> previous{PermissionMode::Default};
    // Set when plan_exit is approved; AgentLoop switches mode and continues
    // the same run with an "execute the plan" message.
    std::atomic<bool> exit_approved{false};
    std::string plan_file;
};

}  // namespace ea::agent