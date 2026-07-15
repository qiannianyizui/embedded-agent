#include <catch2/catch_test_macros.hpp>
#include "provider/OpenAIProvider.h"
#include "provider/OllamaProvider.h"
#include "provider/ProviderFactory.h"
#include "config/Config.h"

using namespace ea;
using namespace ea::provider;

TEST_CASE("OpenAIProvider name", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);
    REQUIRE(provider.name() == "openai_compatible");
}

TEST_CASE("OpenAIProvider config defaults", "[provider]") {
    OpenAIProvider::Config cfg;
    REQUIRE(cfg.base_url == "https://api.openai.com/v1");
    REQUIRE(cfg.default_model == "gpt-4o");
    REQUIRE(cfg.timeout.count() == 60000);
}

TEST_CASE("OpenAIProvider build_request_body basic", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::System, "You are helpful", std::nullopt, std::nullopt, std::nullopt},
        Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "gpt-4o", opts, false);

    REQUIRE(body["model"] == "gpt-4o");
    REQUIRE(body["stream"] == false);
    REQUIRE(body["messages"].size() == 2);
    REQUIRE(body["messages"][0]["role"] == "system");
    REQUIRE(body["messages"][0]["content"] == "You are helpful");
    REQUIRE(body["messages"][1]["role"] == "user");
    REQUIRE(body["messages"][1]["content"] == "Hello");
    REQUIRE(body["temperature"] == 0.7f);
    REQUIRE(body["max_tokens"] == 4096);
}

TEST_CASE("OpenAIProvider build_request_body with tools", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::User, "What's the weather?", std::nullopt, std::nullopt, std::nullopt}
    };
    std::vector<ToolSpec> tools = {
        ToolSpec{"get_weather", "Get weather for a city", json::parse(R"({"type":"object","properties":{"city":{"type":"string"}}})")}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, tools, "gpt-4o", opts, true);

    REQUIRE(body["stream"] == true);
    REQUIRE(body["tools"].size() == 1);
    REQUIRE(body["tools"][0]["type"] == "function");
    REQUIRE(body["tools"][0]["function"]["name"] == "get_weather");
    REQUIRE(body["tools"][0]["function"]["description"] == "Get weather for a city");
}

TEST_CASE("OpenAIProvider build_request_body with tool_calls in message", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    std::vector<ToolCall> calls = {
        ToolCall{"call_123", "get_weather", json::parse(R"({"city":"Paris"})")}
    };
    std::vector<Message> messages = {
        Message{Role::Assistant, "", std::nullopt, calls, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "gpt-4o", opts, false);

    REQUIRE(body["messages"][0]["tool_calls"].size() == 1);
    REQUIRE(body["messages"][0]["tool_calls"][0]["id"] == "call_123");
    REQUIRE(body["messages"][0]["tool_calls"][0]["type"] == "function");
    REQUIRE(body["messages"][0]["tool_calls"][0]["function"]["name"] == "get_weather");
}

TEST_CASE("OpenAIProvider build_request_body with tool result message", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::Tool, "Sunny, 22C", std::nullopt, std::nullopt, std::string{"call_123"}}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "gpt-4o", opts, false);

    REQUIRE(body["messages"][0]["role"] == "tool");
    REQUIRE(body["messages"][0]["content"] == "Sunny, 22C");
    REQUIRE(body["messages"][0]["tool_call_id"] == "call_123");
}

TEST_CASE("OpenAIProvider build_request_body uses default model when empty", "[provider]") {
    OpenAIProvider::Config cfg;
    cfg.default_model = "my-custom-model";
    OpenAIProvider provider(cfg);

    ChatOptions opts;
    auto body = provider.build_request_body({}, {}, "", opts, false);

    REQUIRE(body["model"] == "my-custom-model");
}

TEST_CASE("OpenAIProvider build_request_body with stop option", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    ChatOptions opts;
    opts.stop = "END";
    auto body = provider.build_request_body({}, {}, "gpt-4o", opts, false);

    REQUIRE(body["stop"] == "END");
}

TEST_CASE("OpenAIProvider parse_response basic", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    auto response_json = json::parse(R"({
        "choices": [{
            "message": {
                "role": "assistant",
                "content": "Hello! How can I help?"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 10,
            "completion_tokens": 5
        }
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello! How can I help?");
    REQUIRE(result.value().stop_reason == "stop");
    REQUIRE(result.value().usage.input_tokens == 10);
    REQUIRE(result.value().usage.output_tokens == 5);
    REQUIRE(result.value().tool_calls.empty());
}

TEST_CASE("OpenAIProvider parse_response with tool_calls", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    auto response_json = json::parse(R"({
        "choices": [{
            "message": {
                "role": "assistant",
                "content": null,
                "tool_calls": [{
                    "id": "call_abc",
                    "type": "function",
                    "function": {
                        "name": "get_weather",
                        "arguments": "{\"city\":\"Tokyo\"}"
                    }
                }]
            },
            "finish_reason": "tool_calls"
        }],
        "usage": {
            "prompt_tokens": 20,
            "completion_tokens": 15
        }
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].id == "call_abc");
    REQUIRE(result.value().tool_calls[0].name == "get_weather");
    REQUIRE(result.value().tool_calls[0].arguments["city"] == "Tokyo");
    REQUIRE(result.value().stop_reason == "tool_calls");
}

TEST_CASE("OpenAIProvider parse_response no choices", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    auto response_json = json::parse(R"({"choices": []})");
    auto result = provider.parse_response(response_json);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("OpenAIProvider parse_response missing choices", "[provider]") {
    OpenAIProvider::Config cfg;
    OpenAIProvider provider(cfg);

    auto response_json = json::parse(R"({"error": "bad request"})");
    auto result = provider.parse_response(response_json);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("OllamaProvider stub returns error", "[provider]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);
    REQUIRE(provider.name() == "ollama");

    auto result = provider.chat({}, {}, "test", ChatOptions{});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("OllamaProvider stream_chat stub returns error", "[provider]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto result = provider.stream_chat({}, {}, "test", [](const StreamChunk&) {}, ChatOptions{});
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("OllamaProvider list_models returns empty", "[provider]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto models = provider.list_models();
    REQUIRE(models.empty());
}

TEST_CASE("ProviderFactory creates OpenAI provider", "[provider]") {
    config::ProviderConfig cfg;
    cfg.type = "openai_compatible";
    cfg.base_url = "https://custom.api.com/v1";
    cfg.api_key = "test-key";
    cfg.default_model = "gpt-3.5-turbo";

    auto provider = create(cfg);
    REQUIRE(provider != nullptr);
    REQUIRE(provider->name() == "openai_compatible");
}

TEST_CASE("ProviderFactory creates Ollama provider", "[provider]") {
    config::ProviderConfig cfg;
    cfg.type = "ollama";
    cfg.base_url = "http://localhost:11434";

    auto provider = create(cfg);
    REQUIRE(provider != nullptr);
    REQUIRE(provider->name() == "ollama");
}

TEST_CASE("ProviderFactory returns nullptr for unknown type", "[provider]") {
    config::ProviderConfig cfg;
    cfg.type = "unknown";

    auto provider = create(cfg);
    REQUIRE(provider == nullptr);
}
