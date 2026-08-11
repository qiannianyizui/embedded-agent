#include <catch2/catch_test_macros.hpp>
#include "agent/TurnContext.h"
#include "agent/ITurnStep.h"
#include "agent/LoopDetector.h"
#include "agent/steps/BuildToolSpecsStep.h"
#include "agent/steps/ParseResponseStep.h"
#include "agent/steps/CollectResultsStep.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("BuildToolSpecsStep with null registry", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    BuildToolSpecsStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.tool_specs.empty());
}

TEST_CASE("ParseResponseStep stops on no tool use", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.response.content = "Hello!";
    ctx.response.stop_reason = "stop";

    ParseResponseStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == true);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Role::Assistant);
}

TEST_CASE("ParseResponseStep continues on tool use", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.response.content = "";
    ctx.response.stop_reason = "tool_use";
    ToolCall tc;
    tc.id = "call_1";
    tc.name = "search";
    tc.arguments = json::object();
    ctx.response.tool_calls.push_back(tc);

    ParseResponseStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == false);
    REQUIRE(ctx.pending_tool_calls.size() == 1);
}

TEST_CASE("CollectResultsStep appends tool results to messages", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ToolCall tc;
    tc.id = "call_1";
    tc.name = "search";
    tc.arguments = json::object();
    ctx.pending_tool_calls.push_back(tc);

    ToolResult tr;
    tr.call_id = "call_1";
    tr.output = "search result";
    tr.is_error = false;
    ctx.tool_results.push_back(tr);

    CollectResultsStep step;
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Role::Tool);
    REQUIRE(msgs[0].content == "search result");
    REQUIRE(ctx.pending_tool_calls.empty());
    REQUIRE(ctx.tool_results.empty());
}

TEST_CASE("Step chain with should_stop", "[agent][steps]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    // A step that sets should_stop
    class StopStep : public ITurnStep {
    public:
        Result<void> execute(TurnContext& c) override {
            c.should_stop = true;
            return {};
        }
        std::string name() const override { return "stop"; }
    };

    std::vector<std::unique_ptr<ITurnStep>> steps;
    steps.push_back(std::make_unique<StopStep>());
    steps.push_back(std::make_unique<ParseResponseStep>());  // should not execute

    auto result = run_step_chain(ctx, steps);
    REQUIRE(result.ok());
    REQUIRE(ctx.should_stop == true);
    // ParseResponseStep should not have run (no assistant message added)
    REQUIRE(msgs.empty());
}
