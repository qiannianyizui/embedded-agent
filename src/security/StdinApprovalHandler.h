// src/security/StdinApprovalHandler.h
#pragma once
#include "IApprovalHandler.h"
#include <iostream>
#include <string>

namespace ea::security {

class StdinApprovalHandler : public IApprovalHandler {
public:
    ApprovalDecision request_approval(const ApprovalRequest& req) override {
        std::cout << "\n⚠️  Dangerous tool call requires approval:\n"
                  << "    Tool: " << req.tool_name << "\n"
                  << "    Args: " << req.arguments.dump() << "\n"
                  << "    [y/n/a(abort)] > " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input)) {
            return ApprovalDecision::Aborted;
        }

        if (input == "y" || input == "Y") return ApprovalDecision::Approved;
        if (input == "a" || input == "A") return ApprovalDecision::Aborted;
        return ApprovalDecision::Rejected;
    }
};

}  // namespace ea::security
