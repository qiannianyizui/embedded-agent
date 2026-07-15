#include <catch2/catch_test_macros.hpp>
#include "agent/TurnContext.h"
#include "agent/ITurnStep.h"

using namespace ea;
using namespace ea::agent;

TEST_CASE("TurnContext initialization", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    REQUIRE(ctx.messages.empty());
    REQUIRE(ctx.iteration == 0);
    REQUIRE(ctx.should_stop == false);
    REQUIRE(ctx.interrupted == false);
    REQUIRE(ctx.provider == nullptr);
    REQUIRE(ctx.registry == nullptr);
}

TEST_CASE("TurnContext should_stop flag", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    ctx.should_stop = true;
    REQUIRE(ctx.should_stop == true);
}

TEST_CASE("TurnContext interrupted atomic flag", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    REQUIRE(ctx.interrupted == false);
    ctx.interrupted = true;
    REQUIRE(ctx.interrupted == true);
}

// Simple test step
class TestStep : public ITurnStep {
public:
    Result<void> execute(TurnContext& ctx) override {
        ctx.iteration++;
        return {};
    }
    std::string name() const override { return "test_step"; }
};

TEST_CASE("ITurnStep interface", "[agent][turncontext]") {
    TestStep step;
    REQUIRE(step.name() == "test_step");

    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);
    auto result = step.execute(ctx);
    REQUIRE(result.ok());
    REQUIRE(ctx.iteration == 1);
}

TEST_CASE("run_step_chain executes steps in order", "[agent][turncontext]") {
    std::vector<Message> msgs;
    std::atomic<bool> intr{false};
    TurnContext ctx(msgs, intr);

    std::vector<std::unique_ptr<ITurnStep>> steps;
    steps.push_back(std::make_unique<TestStep>());
    steps.push_back(std::make_unique<TestStep>());
    steps.push_back(std::make_unique<TestStep>());

    auto result = run_step_chain(ctx, steps);
    REQUIRE(result.ok());
    REQUIRE(ctx.iteration == 3);
}
