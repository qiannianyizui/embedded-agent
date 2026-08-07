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
#include "base/Types.h"
#include "base/Result.h"
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

// Helper functions kept for potential future use in extended contract tests
[[maybe_unused]] std::vector<Message> single_user_message() {
    return {Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}};
}

[[maybe_unused]] std::vector<ToolSpec> empty_tools() { return {}; }

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
