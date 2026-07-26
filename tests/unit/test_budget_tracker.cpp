// tests/test_budget_tracker.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "budget/BudgetTracker.h"
#include "provider/IProvider.h"

using namespace ea;
using namespace ea::budget;

// BudgetTestProvider — mock that returns predetermined LLMResponse with usage data
class BudgetTestProvider : public IProvider {
public:
    std::string name() const override { return "budget-test"; }
    std::vector<std::string> list_models() const override { return {"test-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void set_response(LLMResponse resp) { response_ = std::move(resp); }

    Result<LLMResponse> chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        return response_;
    }

    Result<void> stream_chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string& /*model*/,
                              std::function<void(const StreamChunk&)> on_chunk,
                              const ChatOptions&) override {
        // Emit a Content chunk
        StreamChunk content_chunk;
        content_chunk.type = StreamChunk::Type::Content;
        content_chunk.data = "hello";
        on_chunk(content_chunk);

        // Emit a Done chunk with usage
        StreamChunk done_chunk;
        done_chunk.type = StreamChunk::Type::Done;
        done_chunk.usage = response_.usage;
        on_chunk(done_chunk);

        return Result<void>();
    }

private:
    LLMResponse response_;
};

static LLMResponse make_response(int input, int output,
                                  int cache_read = 0, int cache_write = 0) {
    LLMResponse resp;
    resp.content = "test response";
    resp.stop_reason = "stop";
    resp.usage = Usage{input, output, cache_read, cache_write};
    return resp;
}

TEST_CASE("chat intercepts usage", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(100, 50));

    BudgetConfig config;
    BudgetTracker tracker(mock, config);

    auto result = tracker.chat({}, {}, "test-model", {});
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "test response");

    auto usage = tracker.session_usage();
    REQUIRE(usage.input_tokens == 100);
    REQUIRE(usage.output_tokens == 50);
    REQUIRE(usage.cache_read_tokens == 0);
    REQUIRE(usage.cache_write_tokens == 0);
}

TEST_CASE("stream_chat intercepts usage", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(200, 80, 10, 5));

    BudgetConfig config;
    BudgetTracker tracker(mock, config);

    int chunk_count = 0;
    auto result = tracker.stream_chat({}, {}, "test-model",
        [&](const StreamChunk& /*chunk*/) {
            chunk_count++;
        }, {});
    REQUIRE(result.ok());
    REQUIRE(chunk_count == 2);

    auto usage = tracker.session_usage();
    REQUIRE(usage.input_tokens == 200);
    REQUIRE(usage.output_tokens == 80);
    REQUIRE(usage.cache_read_tokens == 10);
    REQUIRE(usage.cache_write_tokens == 5);
}

TEST_CASE("session and global accumulation", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(100, 50));

    BudgetConfig config;
    BudgetTracker tracker(mock, config);

    tracker.chat({}, {}, "test-model", {});
    tracker.chat({}, {}, "test-model", {});

    auto su = tracker.session_usage();
    REQUIRE(su.input_tokens == 200);
    REQUIRE(su.output_tokens == 100);

    auto gu = tracker.global_usage();
    REQUIRE(gu.input_tokens == 200);
    REQUIRE(gu.output_tokens == 100);
}

TEST_CASE("reset_session", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(100, 50));

    BudgetConfig config;
    BudgetTracker tracker(mock, config);

    tracker.chat({}, {}, "test-model", {});
    tracker.reset_session();

    auto su = tracker.session_usage();
    REQUIRE(su.input_tokens == 0);
    REQUIRE(su.output_tokens == 0);

    auto gu = tracker.global_usage();
    REQUIRE(gu.input_tokens == 100);
    REQUIRE(gu.output_tokens == 50);
}

TEST_CASE("model pricing calculate", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(1000, 500));

    BudgetConfig config;
    ModelPricing pricing;
    pricing.model_id = "gpt-4";
    pricing.input_per_mtok = 30.0;   // $30 per 1M input tokens
    pricing.output_per_mtok = 60.0;  // $60 per 1M output tokens
    config.pricing.push_back(pricing);

    BudgetTracker tracker(mock, config);
    tracker.chat({}, {}, "gpt-4", {});

    auto cost = tracker.session_cost();
    // 1000 * 30 / 1,000,000 = 0.03
    REQUIRE(cost.input_cost == Catch::Approx(0.03));
    // 500 * 60 / 1,000,000 = 0.03
    REQUIRE(cost.output_cost == Catch::Approx(0.03));
    REQUIRE(cost.total() == Catch::Approx(0.06));
}

TEST_CASE("model prefix matching", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(1000, 500));

    BudgetConfig config;
    ModelPricing pricing;
    pricing.model_id = "gpt-4o";
    pricing.input_per_mtok = 5.0;
    pricing.output_per_mtok = 15.0;
    config.pricing.push_back(pricing);

    BudgetTracker tracker(mock, config);
    // model "gpt-4o-2024-08-06" should match pricing for "gpt-4o"
    tracker.chat({}, {}, "gpt-4o-2024-08-06", {});

    auto cost = tracker.session_cost();
    // 1000 * 5 / 1,000,000 = 0.005
    REQUIRE(cost.input_cost == Catch::Approx(0.005));
    // 500 * 15 / 1,000,000 = 0.0075
    REQUIRE(cost.output_cost == Catch::Approx(0.0075));
}

TEST_CASE("unmatched model zero cost", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(1000, 500));

    BudgetConfig config;
    ModelPricing pricing;
    pricing.model_id = "claude-3";
    pricing.input_per_mtok = 3.0;
    pricing.output_per_mtok = 15.0;
    config.pricing.push_back(pricing);

    BudgetTracker tracker(mock, config);
    // model "gpt-4" does not match pricing for "claude-3"
    tracker.chat({}, {}, "gpt-4", {});

    auto cost = tracker.session_cost();
    REQUIRE(cost.total() == Catch::Approx(0.0));
}

TEST_CASE("budget warn detection", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(1000, 500));

    BudgetConfig config;
    config.warn_cost_usd = 0.001;  // very low warn threshold
    ModelPricing pricing;
    pricing.model_id = "gpt-4";
    pricing.input_per_mtok = 30.0;
    pricing.output_per_mtok = 60.0;
    config.pricing.push_back(pricing);

    BudgetTracker tracker(mock, config);
    REQUIRE_FALSE(tracker.is_over_warn());

    tracker.chat({}, {}, "gpt-4", {});
    // cost = 0.03 + 0.03 = 0.06 > 0.001
    REQUIRE(tracker.is_over_warn());
}

TEST_CASE("budget limit detection", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(1000, 500));

    BudgetConfig config;
    config.max_cost_usd = 0.001;  // very low max threshold
    ModelPricing pricing;
    pricing.model_id = "gpt-4";
    pricing.input_per_mtok = 30.0;
    pricing.output_per_mtok = 60.0;
    config.pricing.push_back(pricing);

    BudgetTracker tracker(mock, config);
    REQUIRE_FALSE(tracker.is_over_limit());

    tracker.chat({}, {}, "gpt-4", {});
    // cost = 0.06 > 0.001
    REQUIRE(tracker.is_over_limit());
}

TEST_CASE("with no store", "[budget]") {
    auto mock = std::make_shared<BudgetTestProvider>();
    mock->set_response(make_response(100, 50));

    BudgetConfig config;
    // No store set — in-memory only
    BudgetTracker tracker(mock, config);

    auto result = tracker.chat({}, {}, "test-model", {});
    REQUIRE(result.ok());

    auto usage = tracker.session_usage();
    REQUIRE(usage.input_tokens == 100);
    REQUIRE(usage.output_tokens == 50);

    auto cost = tracker.session_cost();
    // No pricing configured, so cost is 0
    REQUIRE(cost.total() == Catch::Approx(0.0));

    // Still works without store
    REQUIRE_FALSE(tracker.is_over_warn());
    REQUIRE_FALSE(tracker.is_over_limit());
}
