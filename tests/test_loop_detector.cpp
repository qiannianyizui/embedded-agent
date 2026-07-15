#include <catch2/catch_test_macros.hpp>
#include "agent/LoopDetector.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("LoopDetector allows normal calls", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "test1"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result1";

    auto action = detector.check(tc, result);
    REQUIRE(action == LoopAction::Continue);
}

TEST_CASE("LoopDetector detects exact repeat", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "same"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // First two calls should be fine
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    // Third identical call triggers Block
    REQUIRE(detector.check(tc, result) == LoopAction::Block);
}

TEST_CASE("LoopDetector detects ping-pong", "[agent][loopdetect]") {
    LoopDetector detector;

    ToolCall tc_a;
    tc_a.id = "1";
    tc_a.name = "tool_a";
    tc_a.arguments = json::object();

    ToolCall tc_b;
    tc_b.id = "2";
    tc_b.name = "tool_b";
    tc_b.arguments = json::object();

    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // Alternating: a, b, a, b, a → 4 alternations → Warn
    detector.check(tc_a, result);
    detector.check(tc_b, result);
    detector.check(tc_a, result);
    detector.check(tc_b, result);
    auto action = detector.check(tc_a, result);
    REQUIRE(action == LoopAction::Warn);
}

TEST_CASE("LoopDetector detects no progress", "[agent][loopdetect]") {
    LoopDetector detector;
    detector.no_progress_threshold = 3;  // Lower for testing

    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "varying"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "same result every time";

    // Same tool, same result, different args
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc, result) == LoopAction::Break);
}

TEST_CASE("LoopDetector reset clears state", "[agent][loopdetect]") {
    LoopDetector detector;
    ToolCall tc;
    tc.id = "1";
    tc.name = "search";
    tc.arguments = json{{"query", "same"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    // Build up some state
    detector.check(tc, result);
    detector.check(tc, result);
    detector.check(tc, result);

    detector.reset();

    // After reset, same calls should not trigger
    REQUIRE(detector.check(tc, result) == LoopAction::Continue);
}

TEST_CASE("LoopDetector different args don't trigger exact repeat", "[agent][loopdetect]") {
    LoopDetector detector;

    ToolCall tc1;
    tc1.id = "1";
    tc1.name = "search";
    tc1.arguments = json{{"query", "query1"}};
    ToolResult result;
    result.call_id = "1";
    result.output = "result";

    ToolCall tc2;
    tc2.id = "2";
    tc2.name = "search";
    tc2.arguments = json{{"query", "query2"}};

    REQUIRE(detector.check(tc1, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc2, result) == LoopAction::Continue);
    REQUIRE(detector.check(tc1, result) == LoopAction::Continue);
}
