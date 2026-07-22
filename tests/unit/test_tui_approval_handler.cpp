// Unit tests for TuiApprovalHandler (thread-safe condition_variable part)
#include <catch2/catch_test_macros.hpp>
#include "tui/TuiApprovalHandler.h"
#include <thread>
#include <chrono>

using namespace ea::tui;
using namespace ea::security;

TEST_CASE("TuiApprovalHandler is not showing initially", "[tui]") {
    TuiApprovalHandler handler;
    REQUIRE_FALSE(handler.is_showing());
    REQUIRE(handler.current_request() == nullptr);
}

TEST_CASE("TuiApprovalHandler blocks until decision set — Rejected", "[tui]") {
    TuiApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "rm -rf /"}}, "Delete everything"};

    ApprovalDecision result = ApprovalDecision::Approved;  // sentinel
    std::thread t([&] { result = handler.request_approval(req); });

    // Spin until handler is showing (request_approval has set showing_ = true)
    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handler.current_request() != nullptr);
    REQUIRE(handler.current_request()->tool_name == "shell");
    REQUIRE(handler.current_request()->description == "Delete everything");

    handler.set_decision(ApprovalDecision::Rejected);
    t.join();
    REQUIRE(result == ApprovalDecision::Rejected);
    REQUIRE_FALSE(handler.is_showing());
}

TEST_CASE("TuiApprovalHandler blocks until decision set — Approved", "[tui]") {
    TuiApprovalHandler handler;
    ApprovalRequest req{"file", nlohmann::json{{"path", "/tmp/test"}}, "Write file"};

    ApprovalDecision result = ApprovalDecision::Rejected;  // sentinel
    std::thread t([&] { result = handler.request_approval(req); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handler.current_request()->tool_name == "file");

    handler.set_decision(ApprovalDecision::Approved);
    t.join();
    REQUIRE(result == ApprovalDecision::Approved);
}

TEST_CASE("TuiApprovalHandler blocks until decision set — Aborted", "[tui]") {
    TuiApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute"};

    ApprovalDecision result = ApprovalDecision::Approved;  // sentinel
    std::thread t([&] { result = handler.request_approval(req); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    handler.set_decision(ApprovalDecision::Aborted);
    t.join();
    REQUIRE(result == ApprovalDecision::Aborted);
}

TEST_CASE("TuiApprovalHandler current_request returns nullptr after decision", "[tui]") {
    TuiApprovalHandler handler;
    ApprovalRequest req{"tool", nlohmann::json::object(), "test"};

    std::thread t([&] { handler.request_approval(req); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handler.current_request() != nullptr);

    handler.set_decision(ApprovalDecision::Approved);
    t.join();

    // After decision, is_showing is false and current_request returns nullptr
    REQUIRE_FALSE(handler.is_showing());
    REQUIRE(handler.current_request() == nullptr);
}

TEST_CASE("TuiApprovalHandler dismiss_dialog does not resolve request", "[tui]") {
    TuiApprovalHandler handler;
    ApprovalRequest req{"tool", nlohmann::json::object(), "test"};

    std::thread t([&] { handler.request_approval(req); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handler.is_showing());

    // Dismiss only clears showing_ flag, does not set decision
    handler.dismiss_dialog();
    REQUIRE_FALSE(handler.is_showing());

    // request_approval is still blocking — must set_decision to unblock
    handler.set_decision(ApprovalDecision::Rejected);
    t.join();
}

TEST_CASE("TuiApprovalHandler sequential approvals", "[tui]") {
    TuiApprovalHandler handler;

    // First approval
    ApprovalRequest req1{"tool_a", nlohmann::json{{"x", 1}}, "first"};
    ApprovalDecision result1 = ApprovalDecision::Rejected;
    std::thread t1([&] { result1 = handler.request_approval(req1); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    handler.set_decision(ApprovalDecision::Approved);
    t1.join();
    REQUIRE(result1 == ApprovalDecision::Approved);

    // Second approval — state should be clean
    REQUIRE_FALSE(handler.is_showing());
    REQUIRE(handler.current_request() == nullptr);

    ApprovalRequest req2{"tool_b", nlohmann::json{{"y", 2}}, "second"};
    ApprovalDecision result2 = ApprovalDecision::Approved;
    std::thread t2([&] { result2 = handler.request_approval(req2); });

    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(handler.current_request()->tool_name == "tool_b");

    handler.set_decision(ApprovalDecision::Aborted);
    t2.join();
    REQUIRE(result2 == ApprovalDecision::Aborted);
}

TEST_CASE("TuiApprovalHandler handles rapid decision after showing", "[tui]") {
    // Tests that set_decision called very soon after request_approval starts
    // works correctly — the condition_variable predicate handles the race.
    TuiApprovalHandler handler;
    ApprovalRequest req{"fast", nlohmann::json::object(), "fast test"};

    ApprovalDecision result = ApprovalDecision::Rejected;
    std::thread t([&] {
        result = handler.request_approval(req);
    });

    // Wait until handler is showing, then set decision immediately
    while (!handler.is_showing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    handler.set_decision(ApprovalDecision::Approved);
    t.join();
    REQUIRE(result == ApprovalDecision::Approved);
}
