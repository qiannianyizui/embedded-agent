// TuiApprovalHandler — thread-safe IApprovalHandler using condition_variable
// for cross-thread communication between AgentLoop and FTXUI main thread.
#pragma once
#include "security/IApprovalHandler.h"
#include <mutex>
#include <condition_variable>
#include <optional>

namespace ea::tui {

class TuiApprovalHandler : public ea::security::IApprovalHandler {
public:
    TuiApprovalHandler();

    /// Called from AgentLoop thread — blocks until a decision is set via set_decision().
    ea::security::ApprovalDecision request_approval(
        const ea::security::ApprovalRequest& req) override;

    /// Called from UI thread — sets the user's decision and unblocks request_approval().
    void set_decision(ea::security::ApprovalDecision decision);

    /// Called from UI thread — dismisses the dialog without a decision (resets state).
    void dismiss_dialog();

    /// Returns the current pending request, or nullptr if no dialog is showing.
    const ea::security::ApprovalRequest* current_request() const;

    /// Returns true when request_approval() is blocking and waiting for a decision.
    bool is_showing() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<ea::security::ApprovalDecision> decision_;
    ea::security::ApprovalRequest current_request_;
    bool showing_ = false;
};

}  // namespace ea::tui
