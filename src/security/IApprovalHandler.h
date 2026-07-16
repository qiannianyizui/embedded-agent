// src/security/IApprovalHandler.h
#pragma once
#include <string>
#include "nlohmann/json.hpp"

namespace ea::security {

enum class ApprovalDecision { Approved, Rejected, Aborted };

struct ApprovalRequest {
    std::string tool_name;
    nlohmann::json arguments;
    std::string description;
};

class IApprovalHandler {
public:
    virtual ~IApprovalHandler() = default;
    virtual ApprovalDecision request_approval(const ApprovalRequest& req) = 0;
};

}  // namespace ea::security
