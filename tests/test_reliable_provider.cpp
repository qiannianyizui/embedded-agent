#include <catch2/catch_test_macros.hpp>
#include "provider/ReliableProvider.h"
#include "core/IProvider.h"

using namespace ea;
using namespace ea::provider;

class FailingProvider : public IProvider {
public:
    std::string name() const override { return "failing"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }

    void set_response(Result<LLMResponse> r) { response_ = std::move(r); }
    int call_count() const { return call_count_; }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        call_count_++;
        return response_;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

private:
    Result<LLMResponse> response_ = LLMResponse{};
    int call_count_ = 0;
};

class SuccessProvider : public IProvider {
public:
    std::string name() const override { return "success"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        LLMResponse resp;
        resp.content = "success response";
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }
};

TEST_CASE("ReliableProvider passes through successful response", "[reliable_provider]") {
    auto primary = std::make_shared<SuccessProvider>();
    ReliableProvider::Config cfg;
    cfg.max_retries = 3;
    ReliableProvider reliable(primary, cfg);

    auto result = reliable.chat({}, {}, "test", {});
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "success response");
}

TEST_CASE("ReliableProvider retries on retryable error", "[reliable_provider]") {
    auto primary = std::make_shared<FailingProvider>();
    primary->set_response(Error::net("server error", 500));

    ReliableProvider::Config cfg;
    cfg.max_retries = 2;
    cfg.base_delay = std::chrono::milliseconds(1);
    ReliableProvider reliable(primary, cfg);

    auto result = reliable.chat({}, {}, "test", {});
    REQUIRE_FALSE(result.ok());
    REQUIRE(primary->call_count() == 3);
}

TEST_CASE("ReliableProvider falls back on non-retryable error", "[reliable_provider]") {
    auto primary = std::make_shared<FailingProvider>();
    primary->set_response(Error::auth("bad key"));

    auto fallback = std::make_shared<SuccessProvider>();

    ReliableProvider::Config cfg;
    cfg.max_retries = 0;
    cfg.fallbacks = {fallback};
    ReliableProvider reliable(primary, cfg);

    auto result = reliable.chat({}, {}, "test", {});
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "success response");
}

TEST_CASE("ReliableProvider returns error when all fallbacks fail", "[reliable_provider]") {
    auto primary = std::make_shared<FailingProvider>();
    primary->set_response(Error::auth("bad key"));

    auto fallback1 = std::make_shared<FailingProvider>();
    fallback1->set_response(Error::auth("also bad"));

    ReliableProvider::Config cfg;
    cfg.max_retries = 0;
    cfg.fallbacks = {fallback1};
    ReliableProvider reliable(primary, cfg);

    auto result = reliable.chat({}, {}, "test", {});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("ReliableProvider capabilities delegates to primary", "[reliable_provider]") {
    auto primary = std::make_shared<SuccessProvider>();
    ReliableProvider::Config cfg;
    ReliableProvider reliable(primary, cfg);

    auto caps = reliable.capabilities();
    REQUIRE(caps.native_tool_calling == true);
    REQUIRE(caps.vision == false);
}

TEST_CASE("ReliableProvider name delegates to primary", "[reliable_provider]") {
    auto primary = std::make_shared<SuccessProvider>();
    ReliableProvider::Config cfg;
    ReliableProvider reliable(primary, cfg);

    REQUIRE(reliable.name() == "success");
}
