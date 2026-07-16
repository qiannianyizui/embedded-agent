// src/security/PendingApprovalHandler.cpp
#include "PendingApprovalHandler.h"
#include "common/io/Logger.h"
#include <chrono>

namespace ea::security {

PendingApprovalHandler::PendingApprovalHandler(int timeout_seconds)
    : timeout_seconds_(timeout_seconds) {}

ApprovalDecision PendingApprovalHandler::request_approval(const ApprovalRequest& req) {
    auto approval = std::make_unique<PendingApproval>();

    // Generate ID
    {
        std::lock_guard<std::mutex> lock(mutex_);
        approval->id = std::to_string(next_id_++);
        approval->request = req;
    }

    EA_INFO("Approval requested: id={}, tool={}", approval->id, req.tool_name);

    PendingApproval* raw_ptr = approval.get();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[approval->id] = std::move(approval);
    }

    // Block until resolved or timeout
    std::unique_lock<std::mutex> lock(raw_ptr->mutex);
    bool timed_out = !raw_ptr->cv.wait_for(lock,
        std::chrono::seconds(timeout_seconds_),
        [raw_ptr] { return raw_ptr->resolved; });

    if (timed_out) {
        EA_WARN("Approval timed out: id={}, tool={}", raw_ptr->id, req.tool_name);
        raw_ptr->decision = ApprovalDecision::Rejected;
    }

    auto result = raw_ptr->decision.load();

    // Clean up resolved approval
    {
        std::lock_guard<std::mutex> map_lock(mutex_);
        pending_.erase(raw_ptr->id);
    }

    return result;
}

std::vector<PendingApproval*> PendingApprovalHandler::pending_list() const {
    std::vector<PendingApproval*> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, approval] : pending_) {
        if (!approval->resolved) {
            result.push_back(approval.get());
        }
    }
    return result;
}

bool PendingApprovalHandler::resolve(const std::string& id, ApprovalDecision decision) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return false;

    auto& approval = it->second;
    approval->decision = decision;
    approval->resolved = true;
    approval->cv.notify_one();

    EA_INFO("Approval resolved: id={}, decision={}", id,
            decision == ApprovalDecision::Approved ? "approved" :
            decision == ApprovalDecision::Rejected ? "rejected" : "aborted");

    return true;
}

size_t PendingApprovalHandler::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, approval] : pending_) {
        if (!approval->resolved) ++count;
    }
    return count;
}

}  // namespace ea::security
