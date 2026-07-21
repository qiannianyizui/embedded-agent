// tests/test_stdin_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/StdinApprovalHandler.h"
#include <sstream>
#include <string>

using namespace ea::security;

// Helper to redirect stdin for testing
class StdinRedirect {
public:
    explicit StdinRedirect(const std::string& input)
        : old_buf_(std::cin.rdbuf()) {
        sstream_.str(input);
        std::cin.rdbuf(sstream_.rdbuf());
    }
    ~StdinRedirect() {
        std::cin.rdbuf(old_buf_);
    }
private:
    std::streambuf* old_buf_;
    std::stringstream sstream_;
};

TEST_CASE("StdinApprovalHandler approves on 'y'", "[approval][stdin]") {
    StdinRedirect redir("y\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Approved);
}

TEST_CASE("StdinApprovalHandler approves on 'Y'", "[approval][stdin]") {
    StdinRedirect redir("Y\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Approved);
}

TEST_CASE("StdinApprovalHandler rejects on 'n'", "[approval][stdin]") {
    StdinRedirect redir("n\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Rejected);
}

TEST_CASE("StdinApprovalHandler rejects on empty input", "[approval][stdin]") {
    StdinRedirect redir("\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Rejected);
}

TEST_CASE("StdinApprovalHandler aborts on 'a'", "[approval][stdin]") {
    StdinRedirect redir("a\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}

TEST_CASE("StdinApprovalHandler aborts on 'A'", "[approval][stdin]") {
    StdinRedirect redir("A\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}

TEST_CASE("StdinApprovalHandler aborts on stdin EOF", "[approval][stdin]") {
    // Empty stream = getline fails = EOF
    StdinRedirect redir("");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}
