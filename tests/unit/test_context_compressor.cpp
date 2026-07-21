// tests/test_context_compressor.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/ContextCompressor.h"
#include "core/IProvider.h"

using namespace ea;
using namespace ea::agent;

// --- Token estimation tests ---

TEST_CASE("estimate_tokens for ASCII text", "[compression]") {
    Message msg{Role::User, "Hello world test", std::nullopt, std::nullopt, std::nullopt};
    int tokens = ContextCompressor::estimate_tokens(msg);
    REQUIRE(tokens >= 3);
    REQUIRE(tokens <= 6);
}

TEST_CASE("estimate_tokens for empty message", "[compression]") {
    Message msg{Role::User, "", std::nullopt, std::nullopt, std::nullopt};
    REQUIRE(ContextCompressor::estimate_tokens(msg) == 0);
}

TEST_CASE("estimate_tokens for empty message list", "[compression]") {
    REQUIRE(ContextCompressor::estimate_tokens(std::vector<Message>{}) == 0);
}

TEST_CASE("estimate_tokens for multiple messages", "[compression]") {
    std::vector<Message> msgs = {
        {Role::System, "You are helpful.", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "Hi there!", std::nullopt, std::nullopt, std::nullopt}
    };
    int total = ContextCompressor::estimate_tokens(msgs);
    int sum = ContextCompressor::estimate_tokens(msgs[0])
            + ContextCompressor::estimate_tokens(msgs[1])
            + ContextCompressor::estimate_tokens(msgs[2]);
    REQUIRE(total == sum);
}

// --- Compression tests with mock provider ---

class MockSummaryProvider : public IProvider {
public:
    std::string name() const override { return "mock-summary"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        call_count_++;
        last_messages_ = msgs;
        if (fail_) return Error::net("summarization failed");

        LLMResponse resp;
        resp.content = "Summary of conversation: user asked about testing.";
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int call_count() const { return call_count_; }
    const std::vector<Message>& last_messages() const { return last_messages_; }
    void set_fail(bool f) { fail_ = f; }

private:
    int call_count_ = 0;
    std::vector<Message> last_messages_;
    bool fail_ = false;
};

TEST_CASE("compress returns messages unchanged when under threshold", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10000;
    config.keep_recent_turns = 4;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs = {
        {Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "Hi!", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 3);
    REQUIRE(provider->call_count() == 0);
}

TEST_CASE("compress returns messages unchanged when disabled", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.enable = false;
    config.max_tokens = 1;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs = {
        {Role::System, "System", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    REQUIRE(provider->call_count() == 0);
}

TEST_CASE("compress triggers summarization when over threshold", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 1;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 10; ++i) {
        msgs.push_back({Role::User, "Question " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "Answer " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(provider->call_count() == 1);
    REQUIRE(result.value().size() < msgs.size());
    REQUIRE(result.value()[0].role == Role::System);
    bool found_summary = false;
    for (const auto& m : result.value()) {
        if (m.content.find("[Conversation Summary]") != std::string::npos) {
            found_summary = true;
        }
    }
    REQUIRE(found_summary);
}

TEST_CASE("compress falls back to truncation on summarization failure", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    provider->set_fail(true);

    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 1;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 10; ++i) {
        msgs.push_back({Role::User, "Question " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "Answer " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() < msgs.size());
    bool found_breadcrumb = false;
    for (const auto& m : result.value()) {
        if (m.content.find("pruned") != std::string::npos) {
            found_breadcrumb = true;
        }
    }
    REQUIRE(found_breadcrumb);
}

TEST_CASE("compress with null provider falls back to truncation", "[compression]") {
    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 1;

    ContextCompressor compressor(nullptr, config);

    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 5; ++i) {
        msgs.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() < msgs.size());
}

TEST_CASE("compress does nothing for short conversation", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 4;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs = {
        {Role::System, "System", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
}

TEST_CASE("CompressionConfig default values", "[compression]") {
    CompressionConfig config;
    REQUIRE(config.max_tokens == 8000);
    REQUIRE(config.keep_recent_turns == 4);
    REQUIRE(config.enable == true);
}
