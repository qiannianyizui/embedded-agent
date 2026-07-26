#include <catch2/catch_test_macros.hpp>
#include "provider/RouterProvider.h"
#include "provider/IProvider.h"

using namespace ea;
using namespace ea::provider;

class StubProvider : public IProvider {
public:
    StubProvider(std::string n, std::string m)
        : name_(std::move(n)), model_(std::move(m)) {}

    std::string name() const override { return name_; }
    std::vector<std::string> list_models() const override { return {model_}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        LLMResponse resp;
        resp.content = name_ + ":" + model_;
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

private:
    std::string name_;
    std::string model_;
};

TEST_CASE("RouterProvider routes by hint", "[router_provider]") {
    auto fast = std::make_shared<StubProvider>("fast-prov", "fast-model");
    auto code = std::make_shared<StubProvider>("code-prov", "code-model");

    RouterProvider::Config cfg;
    cfg.default_provider = fast;
    cfg.rules = {
        RouteRule{"fast", "fast-prov", "fast-model", fast},
        RouteRule{"code", "code-prov", "code-model", code},
    };

    RouterProvider router(cfg);

    ChatOptions opts;
    opts.route_hint = "code";
    auto result = router.chat({}, {}, "", opts);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "code-prov:code-model");
}

TEST_CASE("RouterProvider uses default when no hint", "[router_provider]") {
    auto fast = std::make_shared<StubProvider>("fast-prov", "fast-model");
    auto code = std::make_shared<StubProvider>("code-prov", "code-model");

    RouterProvider::Config cfg;
    cfg.default_provider = fast;
    cfg.rules = {
        RouteRule{"fast", "fast-prov", "fast-model", fast},
        RouteRule{"code", "code-prov", "code-model", code},
    };

    RouterProvider router(cfg);

    ChatOptions opts;
    auto result = router.chat({}, {}, "", opts);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "fast-prov:fast-model");
}

TEST_CASE("RouterProvider uses default for unknown hint", "[router_provider]") {
    auto fast = std::make_shared<StubProvider>("fast-prov", "fast-model");

    RouterProvider::Config cfg;
    cfg.default_provider = fast;
    cfg.rules = {};

    RouterProvider router(cfg);

    ChatOptions opts;
    opts.route_hint = "unknown";
    auto result = router.chat({}, {}, "", opts);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "fast-prov:fast-model");
}

TEST_CASE("RouterProvider capabilities delegates to default", "[router_provider]") {
    auto fast = std::make_shared<StubProvider>("fast-prov", "fast-model");

    RouterProvider::Config cfg;
    cfg.default_provider = fast;

    RouterProvider router(cfg);
    REQUIRE(router.capabilities().native_tool_calling == true);
}

TEST_CASE("RouterProvider name returns router", "[router_provider]") {
    auto fast = std::make_shared<StubProvider>("fast-prov", "fast-model");
    RouterProvider::Config cfg;
    cfg.default_provider = fast;

    RouterProvider router(cfg);
    REQUIRE(router.name() == "router");
}
