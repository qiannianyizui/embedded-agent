// TuiApprovalHandler — implementation
#include "TuiApprovalHandler.h"

namespace ea::tui {

TuiApprovalHandler::TuiApprovalHandler() = default;

ea::security::ApprovalDecision TuiApprovalHandler::request_approval(
    const ea::security::ApprovalRequest& req) {
    std::unique_lock<std::mutex> lock(mutex_);
    current_request_ = req;
    showing_ = true;
    decision_.reset();

    // Block until set_decision() provides a value
    cv_.wait(lock, [this] { return decision_.has_value(); });

    showing_ = false;
    return decision_.value();
}

void TuiApprovalHandler::set_decision(ea::security::ApprovalDecision decision) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decision_ = decision;
        showing_ = false;
    }
    cv_.notify_one();
}

void TuiApprovalHandler::dismiss_dialog() {
    std::lock_guard<std::mutex> lock(mutex_);
    showing_ = false;
    // Note: does NOT set decision_ — request_approval() will keep blocking
    // until set_decision() is called. This is intentional: dismiss_dialog
    // is for UI cleanup only, not for resolving the approval request.
}

const ea::security::ApprovalRequest* TuiApprovalHandler::current_request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (showing_) {
        return &current_request_;
    }
    return nullptr;
}

bool TuiApprovalHandler::is_showing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return showing_;
}

}  // namespace ea::tui
