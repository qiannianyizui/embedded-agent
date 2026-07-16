// tests/test_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/IApprovalHandler.h"

using namespace ea::security;

// MockApprovalHandler for testing
class MockApprovalHandler : public IApprovalHandler {
public:
    ApprovalDecision next_decision = ApprovalDecision::Approved;
    mutable ApprovalRequest last_request;

    ApprovalDecision request_approval(const ApprovalRequest& req) override {
        last_request = req;
        return next_decision;
    }
};

TEST_CASE("MockApprovalHandler returns Approved by default", "[approval]") {
    MockApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Approved);
}

TEST_CASE("MockApprovalHandler returns Rejected when configured", "[approval]") {
    MockApprovalHandler handler;
    handler.next_decision = ApprovalDecision::Rejected;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "rm -rf /"}}, "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("MockApprovalHandler returns Aborted when configured", "[approval]") {
    MockApprovalHandler handler;
    handler.next_decision = ApprovalDecision::Aborted;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Aborted);
}

TEST_CASE("MockApprovalHandler captures last request", "[approval]") {
    MockApprovalHandler handler;
    ApprovalRequest req{"file", nlohmann::json{{"path", "/tmp/test"}}, "Write file"};
    handler.request_approval(req);
    REQUIRE(handler.last_request.tool_name == "file");
    REQUIRE(handler.last_request.arguments["path"] == "/tmp/test");
    REQUIRE(handler.last_request.description == "Write file");
}

TEST_CASE("ApprovalRequest default values", "[approval]") {
    ApprovalRequest req;
    REQUIRE(req.tool_name.empty());
    REQUIRE(req.description.empty());
    REQUIRE(req.arguments.is_null());
}
