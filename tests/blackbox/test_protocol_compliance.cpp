// tests/blackbox/test_protocol_compliance.cpp
// Protocol compliance tests — verify Provider parse_response() correctly handles
// real API response formats from OpenAI, Anthropic, and Ollama.
// Uses fixture JSON files — no network calls needed.
#include <catch2/catch_test_macros.hpp>
#include "nlohmann/json.hpp"
#include "provider/OpenAIProvider.h"
#include "provider/AnthropicProvider.h"
#include "provider/OllamaProvider.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <fstream>
#include <string>

using namespace ea;
using json = nlohmann::json;

namespace {

json load_fixture(const std::string& name) {
    std::string path = std::string(FIXTURES_DIR) + "/" + name;
    std::ifstream f(path);
    if (!f.is_open()) {
        path = std::string(CMAKE_SOURCE_DIR) + "/tests/blackbox/fixtures/" + name;
        f.open(path);
    }
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    return json::parse(content);
}

provider::OpenAIProvider make_openai() {
    provider::OpenAIProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test-key";
    return provider::OpenAIProvider(cfg);
}

provider::AnthropicProvider make_anthropic() {
    provider::AnthropicProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test-key";
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

// --- OpenAI Protocol ---

TEST_CASE("Protocol: OpenAI text response parsed correctly", "[protocol][blackbox][openai]") {
    auto provider = make_openai();
    auto fixture = load_fixture("openai_text.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.content == "Hello! How can I help you today?");
    REQUIRE(resp.stop_reason == "stop");
    REQUIRE(resp.usage.input_tokens == 15);
    REQUIRE(resp.usage.output_tokens == 8);
}

TEST_CASE("Protocol: OpenAI tool call response parsed correctly", "[protocol][blackbox][openai]") {
    auto provider = make_openai();
    auto fixture = load_fixture("openai_tool_call.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.tool_calls.size() == 1);
    REQUIRE(resp.tool_calls[0].id == "call_abc123");
    REQUIRE(resp.tool_calls[0].name == "shell");
    REQUIRE(resp.stop_reason == "tool_calls");
}

TEST_CASE("Protocol: OpenAI malformed missing_choices returns error", "[protocol][blackbox][openai]") {
    auto provider = make_openai();
    auto fixture = load_fixture("malformed/missing_choices.json");
    auto result = provider.parse_response(fixture);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Protocol: OpenAI malformed wrong_type_fields returns error", "[protocol][blackbox][openai]") {
    auto provider = make_openai();
    auto fixture = load_fixture("malformed/wrong_type_fields.json");
    auto result = provider.parse_response(fixture);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Protocol: OpenAI malformed truncated JSON throws", "[protocol][blackbox][openai]") {
    // Truncated JSON can't be parsed by json::parse itself
    std::string path = std::string(FIXTURES_DIR) + "/malformed/truncated.json";
    std::ifstream f(path);
    if (!f.is_open()) {
        path = std::string(CMAKE_SOURCE_DIR) + "/tests/blackbox/fixtures/malformed/truncated.json";
        f.open(path);
    }
    REQUIRE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    REQUIRE_THROWS(json::parse(content));
}

TEST_CASE("Protocol: OpenAI build_request_body has correct format", "[protocol][blackbox][openai]") {
    auto provider = make_openai();
    auto msgs = single_user_message();
    auto body = provider.build_request_body(msgs, empty_tools(), "gpt-4o", ChatOptions{}, false);
    REQUIRE(body.contains("model"));
    REQUIRE(body["model"] == "gpt-4o");
    REQUIRE(body.contains("messages"));
    REQUIRE(body["messages"].is_array());
    REQUIRE(body["messages"].size() == 1);
}

// --- Anthropic Protocol ---

TEST_CASE("Protocol: Anthropic text response parsed correctly", "[protocol][blackbox][anthropic]") {
    auto provider = make_anthropic();
    auto fixture = load_fixture("anthropic_text.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.content == "Hello! How can I help you today?");
    REQUIRE(resp.stop_reason == "end_turn");
    REQUIRE(resp.usage.input_tokens == 15);
    REQUIRE(resp.usage.output_tokens == 8);
}

TEST_CASE("Protocol: Anthropic tool use response parsed correctly", "[protocol][blackbox][anthropic]") {
    auto provider = make_anthropic();
    auto fixture = load_fixture("anthropic_tool_use.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.tool_calls.size() == 1);
    REQUIRE(resp.tool_calls[0].name == "shell");
    REQUIRE(resp.stop_reason == "tool_use");
}

TEST_CASE("Protocol: Anthropic error response returns error", "[protocol][blackbox][anthropic]") {
    auto provider = make_anthropic();
    auto fixture = load_fixture("anthropic_error.json");
    auto result = provider.parse_response(fixture);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Protocol: Anthropic build_request_body extracts system to top-level", "[protocol][blackbox][anthropic]") {
    auto provider = make_anthropic();
    std::vector<Message> msgs = {
        Message{Role::System, "You are helpful", std::nullopt, std::nullopt, std::nullopt},
        Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };
    auto body = provider.build_request_body(msgs, empty_tools(), "claude-sonnet-4-6", ChatOptions{}, false);
    REQUIRE(body.contains("system"));
    REQUIRE(body["system"].is_string());
    REQUIRE(body.contains("messages"));
    // System message should be extracted, not in messages array
    for (const auto& m : body["messages"]) {
        REQUIRE(m["role"] != "system");
    }
}

// --- Ollama Protocol ---

TEST_CASE("Protocol: Ollama text response parsed correctly", "[protocol][blackbox][ollama]") {
    auto provider = make_ollama();
    auto fixture = load_fixture("ollama_text.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.content == "Hello! How can I help you today?");
    REQUIRE(resp.usage.input_tokens == 15);
    REQUIRE(resp.usage.output_tokens == 8);
}

TEST_CASE("Protocol: Ollama tool call response parsed correctly", "[protocol][blackbox][ollama]") {
    auto provider = make_ollama();
    auto fixture = load_fixture("ollama_tool_call.json");
    auto result = provider.parse_response(fixture);
    REQUIRE(result.ok());
    auto& resp = result.value();
    REQUIRE(resp.tool_calls.size() == 1);
    REQUIRE(resp.tool_calls[0].name == "shell");
}

TEST_CASE("Protocol: Ollama build_request_body has correct format", "[protocol][blackbox][ollama]") {
    auto provider = make_ollama();
    auto msgs = single_user_message();
    auto body = provider.build_request_body(msgs, empty_tools(), "llama3", ChatOptions{}, false);
    REQUIRE(body.contains("model"));
    REQUIRE(body["model"] == "llama3");
    REQUIRE(body.contains("messages"));
    REQUIRE(body["messages"].is_array());
}
