// src/security/PendingApprovalHandler.h
#pragma once
#include "IApprovalHandler.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ea::security {

struct PendingApproval {
    std::string id;
    ApprovalRequest request;
    std::atomic<ApprovalDecision> decision{ApprovalDecision::Rejected};
    std::condition_variable cv;
    std::mutex mutex;
    bool resolved = false;
};

class PendingApprovalHandler : public IApprovalHandler {
public:
    explicit PendingApprovalHandler(int timeout_seconds = 300);

    ApprovalDecision request_approval(const ApprovalRequest& req) override;

    // Server-side: list pending approvals
    std::vector<PendingApproval*> pending_list() const;

    // Server-side: resolve an approval by id
    bool resolve(const std::string& id, ApprovalDecision decision);

    // Number of pending (unresolved) approvals
    size_t pending_count() const;

private:
    int timeout_seconds_;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<PendingApproval>> pending_;
    int next_id_ = 1;
};

}  // namespace ea::security
