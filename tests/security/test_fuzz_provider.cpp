// tests/security/test_fuzz_provider.cpp
#include <catch2/catch_test_macros.hpp>
#include "FuzzHelper.h"
#include "base/Types.h"
#include "MockProvider.h"
#include "MockTool.h"
#include "agent/AgentLoop.h"
#include "tool/ToolRegistry.h"
#include "net/SseParser.h"

using namespace ea;
using namespace ea::test;
using namespace ea::agent;

TEST_CASE("Fuzz: AgentLoop handles random LLMResponse content without crashing",
          "[fuzz][security]") {
    FuzzGenerator gen(321);
    tool::ToolRegistry registry;

    for (int i = 0; i < 100; ++i) {
        MockProvider provider;
        LLMResponse resp;
        resp.content = gen.random_string(0, 4096);
        resp.stop_reason = "stop";
        provider.enqueue(std::move(resp));

        std::string output;
        AgentLoop loop(&provider, &registry, nullptr,
                       AgentLoop::Config{},
                       [&](const std::string& t) { output = t; });

        auto result = loop.run("test");
        // Must not crash
        (void)result;
    }
    REQUIRE(true);
}

TEST_CASE("Fuzz: AgentLoop handles random tool calls without crashing",
          "[fuzz][security]") {
    FuzzGenerator gen(654);
    tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<MockTool>("fuzz_tool", "ok", false, false));

    for (int i = 0; i < 50; ++i) {
        MockProvider provider;
        // Random tool call
        ToolCall tc;
        tc.id = "call_" + std::to_string(i);
        tc.name = "fuzz_tool";
        auto parsed = json::parse(gen.random_json(3), nullptr, false);
        tc.arguments = parsed.is_object() ? parsed : json::object();
        provider.enqueue_tool_calls({tc});
        provider.enqueue_text("Done");

        std::string output;
        AgentLoop loop(&provider, &registry, nullptr,
                       AgentLoop::Config{},
                       [&](const std::string& t) { output = t; });

        auto result = loop.run("test");
        // Must not crash
        (void)result;
    }
    REQUIRE(true);
}

TEST_CASE("Fuzz: SSE parser handles random streams without crashing",
          "[fuzz][security]") {
    FuzzGenerator gen(987);
    for (int i = 0; i < 100; ++i) {
        std::string stream = gen.random_sse_stream(10);
        net::SseParser parser;
        int event_count = 0;
        // Feed the random SSE stream to the parser and count events.
        // Must not crash regardless of input content.
        parser.feed(stream, [&](const net::SseEvent& event) {
            (void)event;
            event_count++;
        });
        // Verify the parser consumed the stream without crashing.
        // The event count may vary since random content may or may not
        // produce valid SSE events, but the parser must not crash.
        (void)event_count;
    }
    REQUIRE(true);
}
