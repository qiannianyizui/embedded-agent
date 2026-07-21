#include <catch2/catch_test_macros.hpp>
#include "provider/AnthropicProvider.h"
#include "provider/ProviderFactory.h"
#include "config/Config.h"

using namespace ea;
using namespace ea::provider;

TEST_CASE("AnthropicProvider name", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);
    REQUIRE(provider.name() == "anthropic");
}

TEST_CASE("AnthropicProvider config defaults", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    REQUIRE(cfg.base_url == "https://api.anthropic.com");
    REQUIRE(cfg.default_model == "claude-sonnet-4-6");
    REQUIRE(cfg.api_version == "2023-06-01");
    REQUIRE(cfg.timeout.count() == 60000);
}

TEST_CASE("AnthropicProvider build_request_body basic", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::System, "You are helpful", std::nullopt, std::nullopt, std::nullopt},
        Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "claude-sonnet-4-6", opts, false);

    REQUIRE(body["model"] == "claude-sonnet-4-6");
    REQUIRE(body["stream"] == false);
    // System message extracted to top-level field
    REQUIRE(body["system"] == "You are helpful\n");
    // Only user message in messages array
    REQUIRE(body["messages"].size() == 1);
    REQUIRE(body["messages"][0]["role"] == "user");
    REQUIRE(body["messages"][0]["content"] == "Hello");
}

TEST_CASE("AnthropicProvider build_request_body with tools", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::User, "What's the weather?", std::nullopt, std::nullopt, std::nullopt}
    };
    std::vector<ToolSpec> tools = {
        ToolSpec{"get_weather", "Get weather", json::parse(R"({"type":"object","properties":{"city":{"type":"string"}}})")}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, tools, "claude-sonnet-4-6", opts, true);

    REQUIRE(body["stream"] == true);
    REQUIRE(body["tools"].size() == 1);
    REQUIRE(body["tools"][0]["name"] == "get_weather");
    REQUIRE(body["tools"][0]["description"] == "Get weather");
    REQUIRE(body["tools"][0]["input_schema"]["type"] == "object");
}

TEST_CASE("AnthropicProvider build_request_body assistant with tool_calls", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<ToolCall> calls = {
        ToolCall{"call_123", "get_weather", json::parse(R"({"city":"Paris"})")}
    };
    std::vector<Message> messages = {
        Message{Role::Assistant, "Let me check", std::nullopt, calls, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "claude-sonnet-4-6", opts, false);

    REQUIRE(body["messages"][0]["role"] == "assistant");
    auto content = body["messages"][0]["content"];
    // Should have text block + tool_use block
    bool has_text = false, has_tool_use = false;
    for (const auto& block : content) {
        if (block["type"] == "text") has_text = true;
        if (block["type"] == "tool_use") {
            has_tool_use = true;
            REQUIRE(block["id"] == "call_123");
            REQUIRE(block["name"] == "get_weather");
            REQUIRE(block["input"]["city"] == "Paris");
        }
    }
    REQUIRE(has_text);
    REQUIRE(has_tool_use);
}

TEST_CASE("AnthropicProvider build_request_body tool result message", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::Tool, "Sunny, 22C", std::nullopt, std::nullopt, std::string{"call_123"}}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "claude-sonnet-4-6", opts, false);

    // Tool result becomes user role with tool_result content block
    REQUIRE(body["messages"][0]["role"] == "user");
    auto content = body["messages"][0]["content"];
    REQUIRE(content.size() == 1);
    REQUIRE(content[0]["type"] == "tool_result");
    REQUIRE(content[0]["tool_use_id"] == "call_123");
    REQUIRE(content[0]["content"] == "Sunny, 22C");
}

TEST_CASE("AnthropicProvider build_request_body tool result with error", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::Tool, "Error: something failed", std::nullopt, std::nullopt, std::string{"call_456"}}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "claude-sonnet-4-6", opts, false);

    auto content = body["messages"][0]["content"];
    REQUIRE(content[0]["is_error"] == true);
}

TEST_CASE("AnthropicProvider build_request_body uses default model when empty", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    cfg.default_model = "claude-haiku-4-5";
    AnthropicProvider provider(cfg);

    ChatOptions opts;
    auto body = provider.build_request_body({}, {}, "", opts, false);

    REQUIRE(body["model"] == "claude-haiku-4-5");
}

TEST_CASE("AnthropicProvider build_request_body empty assistant gets empty text block", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::Assistant, "", std::nullopt, std::nullopt, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "claude-sonnet-4-6", opts, false);

    auto content = body["messages"][0]["content"];
    REQUIRE(content.size() == 1);
    REQUIRE(content[0]["type"] == "text");
    REQUIRE(content[0]["text"] == "");
}

TEST_CASE("AnthropicProvider parse_response basic text", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    auto response_json = json::parse(R"({
        "content": [{"type": "text", "text": "Hello! How can I help?"}],
        "stop_reason": "end_turn",
        "usage": {"input_tokens": 10, "output_tokens": 5}
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello! How can I help?");
    REQUIRE(result.value().stop_reason == "end_turn");
    REQUIRE(result.value().usage.input_tokens == 10);
    REQUIRE(result.value().usage.output_tokens == 5);
    REQUIRE(result.value().tool_calls.empty());
}

TEST_CASE("AnthropicProvider parse_response with tool_use", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    auto response_json = json::parse(R"({
        "content": [
            {"type": "text", "text": "Let me check."},
            {"type": "tool_use", "id": "call_abc", "name": "get_weather", "input": {"city": "Tokyo"}}
        ],
        "stop_reason": "tool_use",
        "usage": {"input_tokens": 20, "output_tokens": 15}
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Let me check.");
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].id == "call_abc");
    REQUIRE(result.value().tool_calls[0].name == "get_weather");
    REQUIRE(result.value().tool_calls[0].arguments["city"] == "Tokyo");
    REQUIRE(result.value().stop_reason == "tool_use");
}

TEST_CASE("AnthropicProvider parse_response error", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    auto response_json = json::parse(R"({
        "error": {"message": "Invalid API key", "status_code": 401}
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::NetworkError);
    REQUIRE(result.error().http_status == 401);
}

TEST_CASE("AnthropicProvider parse_response with cache tokens", "[provider][anthropic]") {
    AnthropicProvider::Config cfg;
    AnthropicProvider provider(cfg);

    auto response_json = json::parse(R"({
        "content": [{"type": "text", "text": "hi"}],
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 100,
            "output_tokens": 10,
            "cache_read_input_tokens": 50,
            "cache_creation_input_tokens": 20
        }
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().usage.cache_read_tokens == 50);
    REQUIRE(result.value().usage.cache_write_tokens == 20);
}

TEST_CASE("ProviderFactory creates Anthropic provider", "[provider]") {
    config::ProviderConfig cfg;
    cfg.type = "anthropic";
    cfg.api_key = "test-key";

    auto provider = create(cfg);
    REQUIRE(provider != nullptr);
    REQUIRE(provider->name() == "anthropic");
}
