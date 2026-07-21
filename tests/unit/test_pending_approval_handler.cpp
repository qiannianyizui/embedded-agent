// tests/test_pending_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/PendingApprovalHandler.h"
#include <chrono>
#include <thread>

using namespace ea::security;

TEST_CASE("PendingApprovalHandler blocks until resolved", "[approval][pending]") {
    PendingApprovalHandler handler(10);  // 10s timeout

    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};

    // Resolve in background thread after a short delay
    std::string approval_id;
    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Find the pending approval
        auto pending = handler.pending_list();
        REQUIRE(pending.size() == 1);
        approval_id = pending[0]->id;
        handler.resolve(approval_id, ApprovalDecision::Approved);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Approved);
}

TEST_CASE("PendingApprovalHandler rejects on timeout", "[approval][pending]") {
    PendingApprovalHandler handler(1);  // 1s timeout for fast test

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    // No resolver — let it timeout
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("PendingApprovalHandler resolve with Rejected", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        REQUIRE(pending.size() == 1);
        handler.resolve(pending[0]->id, ApprovalDecision::Rejected);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("PendingApprovalHandler resolve with Aborted", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        handler.resolve(pending[0]->id, ApprovalDecision::Aborted);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Aborted);
}

TEST_CASE("PendingApprovalHandler resolve returns false for unknown id", "[approval][pending]") {
    PendingApprovalHandler handler(10);
    REQUIRE(handler.resolve("nonexistent", ApprovalDecision::Approved) == false);
}

TEST_CASE("PendingApprovalHandler pending_count is zero when empty", "[approval][pending]") {
    PendingApprovalHandler handler(10);
    REQUIRE(handler.pending_count() == 0);
}

TEST_CASE("PendingApprovalHandler cleans up after resolution", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        handler.resolve(pending[0]->id, ApprovalDecision::Approved);
    });

    handler.request_approval(req);
    resolver.join();

    // After resolution, pending should be cleaned up
    REQUIRE(handler.pending_count() == 0);
}
