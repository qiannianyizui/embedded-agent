// tests/blackbox/test_provider_contract.cpp
// IProvider contract compliance tests — verify that all provider implementations
// satisfy the IProvider behavioral contract. Uses MockProvider for chat/stream
// verification; direct providers (OpenAI/Anthropic/Ollama) are tested for
// name(), capabilities(), parse_response(), and build_request_body() only
// (no network calls).
#include <catch2/catch_test_macros.hpp>
#include "ContractTestHelper.h"
#include "MockProvider.h"
#include "TestHelpers.h"
#include "provider/OpenAIProvider.h"
#include "provider/AnthropicProvider.h"
#include "provider/OllamaProvider.h"
#include "provider/ReliableProvider.h"
#include "plugin/PluginProviderAdapter.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include "nlohmann/json.hpp"

using namespace ea;
using namespace ea::test;
using namespace ea::test::contract;
using json = nlohmann::json;

namespace {

provider::OpenAIProvider make_openai() {
    provider::OpenAIProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test";
    return provider::OpenAIProvider(cfg);
}

provider::AnthropicProvider make_anthropic() {
    provider::AnthropicProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test";
    return provider::AnthropicProvider(cfg);
}

provider::OllamaProvider make_ollama() {
    provider::OllamaProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    return provider::OllamaProvider(cfg);
}

std::vector<Message> single_user_message() {
    return {Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}};
}

std::vector<ToolSpec> empty_tools() { return {}; }

}  // anonymous namespace

// --- MockProvider contract ---

TEST_CASE("MockProvider satisfies provider contract", "[contract][greybox][provider]") {
    auto mock = std::make_unique<MockProvider>();
    mock->enqueue_text("contract test response");
    ProviderContract::verify_all(*mock);
}

// --- OpenAI direct provider (no network) ---

TEST_CASE("OpenAI name() returns openai_compatible", "[contract][greybox][provider]") {
    auto provider = make_openai();
    REQUIRE(provider.name() == "openai_compatible");
}

TEST_CASE("OpenAI capabilities() returns expected values", "[contract][greybox][provider]") {
    auto provider = make_openai();
    auto caps = provider.capabilities();
    REQUIRE(caps.native_tool_calling == true);
    REQUIRE(caps.streaming == true);
    REQUIRE(caps.vision == true);
    REQUIRE(caps.prompt_caching == false);
    REQUIRE(caps.extended_thinking == false);
}

// --- Anthropic direct provider (no network) ---

TEST_CASE("Anthropic name() returns anthropic", "[contract][greybox][provider]") {
    auto provider = make_anthropic();
    REQUIRE(provider.name() == "anthropic");
}

TEST_CASE("Anthropic capabilities() returns expected values", "[contract][greybox][provider]") {
    auto provider = make_anthropic();
    auto caps = provider.capabilities();
    REQUIRE(caps.native_tool_calling == true);
    REQUIRE(caps.streaming == true);
    REQUIRE(caps.vision == true);
    REQUIRE(caps.prompt_caching == true);
    REQUIRE(caps.extended_thinking == true);
}

// --- Ollama direct provider (no network) ---

TEST_CASE("Ollama name() returns ollama", "[contract][greybox][provider]") {
    auto provider = make_ollama();
    REQUIRE(provider.name() == "ollama");
}

TEST_CASE("Ollama capabilities() returns expected values", "[contract][greybox][provider]") {
    auto provider = make_ollama();
    auto caps = provider.capabilities();
    REQUIRE(caps.native_tool_calling == true);
    REQUIRE(caps.streaming == true);
    REQUIRE(caps.vision == false);
    REQUIRE(caps.prompt_caching == false);
    REQUIRE(caps.extended_thinking == false);
}

// --- ReliableProvider wrapping MockProvider ---

TEST_CASE("ReliableProvider with MockProvider satisfies contract", "[contract][greybox][provider]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_text("reliable response");
    provider::ReliableProvider::Config cfg;
    cfg.max_retries = 0;  // No retries needed for mock
    cfg.base_delay = std::chrono::milliseconds(1);
    provider::ReliableProvider reliable(mock, cfg);
    ProviderContract::verify_all(reliable);
}

// --- PluginProviderAdapter wrapping MockProvider ---

TEST_CASE("PluginProviderAdapter with MockProvider satisfies contract", "[contract][greybox][provider]") {
    auto mock = std::make_shared<MockProvider>();
    mock->enqueue_text("plugin response");
    // nullptr so_handle — no actual .so to keep loaded
    plugin::PluginProviderAdapter adapter(mock, nullptr);
    ProviderContract::verify_all(adapter);
}

TEST_CASE("PluginProviderAdapter catches exceptions from failing provider", "[contract][greybox][provider]") {
    // Create a provider that throws on every call
    class ThrowingProvider : public IProvider {
    public:
        std::string name() const override { throw std::runtime_error("name crash"); }
        std::vector<std::string> list_models() const override { throw std::runtime_error("list crash"); }
        provider::ProviderCapabilities capabilities() const override {
            throw std::runtime_error("caps crash");
        }
        Result<LLMResponse> chat(
            const std::vector<Message>&, const std::vector<ToolSpec>&,
            const std::string&, const ChatOptions&) override
        {
            throw std::runtime_error("chat crash");
        }
        Result<void> stream_chat(
            const std::vector<Message>&, const std::vector<ToolSpec>&,
            const std::string&, std::function<void(const StreamChunk&)>,
            const ChatOptions&) override
        {
            throw std::runtime_error("stream crash");
        }
    };

    auto throwing = std::make_shared<ThrowingProvider>();
    plugin::PluginProviderAdapter adapter(throwing, nullptr);

    // name() should not throw — returns fallback
    REQUIRE(adapter.name() == "<plugin-error>");

    // list_models() should not throw — returns empty
    REQUIRE(adapter.list_models().empty());

    // capabilities() should not throw — returns all-false
    auto caps = adapter.capabilities();
    REQUIRE(caps.native_tool_calling == false);
    REQUIRE(caps.streaming == false);
    REQUIRE(caps.vision == false);
    REQUIRE(caps.prompt_caching == false);
    REQUIRE(caps.extended_thinking == false);

    // chat() should not throw — returns plugin error
    std::vector<Message> msgs = {Message{Role::User, "test", std::nullopt, std::nullopt, std::nullopt}};
    auto chat_result = adapter.chat(msgs, {}, "model");
    REQUIRE_FALSE(chat_result.ok());
    REQUIRE(chat_result.error().code == ErrorCode::PluginError);

    // stream_chat() should not throw — returns plugin error
    auto stream_result = adapter.stream_chat(msgs, {}, "model",
        [](const StreamChunk&) {}, {});
    REQUIRE_FALSE(stream_result.ok());
    REQUIRE(stream_result.error().code == ErrorCode::PluginError);
}
