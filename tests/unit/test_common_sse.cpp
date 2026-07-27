#include <catch2/catch_test_macros.hpp>
#include "net/SseParser.h"

using namespace ea::net;

TEST_CASE("SseParser parses single event", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    parser.feed("data: hello\n\n", [&](const SseEvent& e) { events.push_back(e); });
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser parses event with type", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    parser.feed("event: message\ndata: hello\n\n", [&](const SseEvent& e) { events.push_back(e); });
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event == "message");
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser handles multi-line data", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    parser.feed("data: line1\ndata: line2\n\n", [&](const SseEvent& e) { events.push_back(e); });
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "line1\nline2");
}

TEST_CASE("SseParser handles incremental chunks", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    auto on_event = [&](const SseEvent& e) { events.push_back(e); };
    parser.feed("data: hel", on_event);
    REQUIRE(events.size() == 0);
    parser.feed("lo\n\n", on_event);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser ignores comments", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    parser.feed(": this is a comment\ndata: hello\n\n", [&](const SseEvent& e) { events.push_back(e); });
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser handles [DONE] marker", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    parser.feed("data: [DONE]\n\n", [&](const SseEvent& e) { events.push_back(e); });
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "[DONE]");
}
