#include <catch2/catch_test_macros.hpp>
#include "provider/OllamaProvider.h"
#include "common/base/Types.h"

using namespace ea;
using namespace ea::provider;

TEST_CASE("OllamaProvider build_request_body produces correct Ollama format", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    cfg.default_model = "llama3";
    OllamaProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::System, "You are helpful", std::nullopt, std::nullopt, std::nullopt},
        Message{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };
    ChatOptions opts;
    opts.temperature = 0.5f;
    opts.top_p = 0.9f;
    opts.max_tokens = 2048;

    auto body = provider.build_request_body(messages, {}, "llama3", opts, false);

    REQUIRE(body["model"] == "llama3");
    REQUIRE(body["stream"] == false);
    REQUIRE(body["messages"].size() == 2);
    REQUIRE(body["messages"][0]["role"] == "system");
    REQUIRE(body["messages"][0]["content"] == "You are helpful");
    REQUIRE(body["messages"][1]["role"] == "user");
    REQUIRE(body["messages"][1]["content"] == "Hello");
    // Ollama uses nested "options" object instead of top-level params
    REQUIRE(body["options"]["temperature"] == 0.5f);
    REQUIRE(body["options"]["top_p"] == 0.9f);
    REQUIRE(body["options"]["num_predict"] == 2048);
}

TEST_CASE("OllamaProvider build_request_body converts tool result messages with tool_name", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::Tool, "Sunny, 22C", std::string{"get_weather"}, std::nullopt, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "llama3", opts, false);

    REQUIRE(body["messages"][0]["role"] == "tool");
    REQUIRE(body["messages"][0]["tool_name"] == "get_weather");
    REQUIRE(body["messages"][0]["content"] == "Sunny, 22C");
    // Should NOT have tool_call_id — Ollama uses tool_name instead
    REQUIRE_FALSE(body["messages"][0].contains("tool_call_id"));
}

TEST_CASE("OllamaProvider build_request_body includes tools in OpenAI format", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    std::vector<Message> messages = {
        Message{Role::User, "What's the weather?", std::nullopt, std::nullopt, std::nullopt}
    };
    std::vector<ToolSpec> tools = {
        ToolSpec{"get_weather", "Get weather for a city", json::parse(R"({"type":"object","properties":{"city":{"type":"string"}}})")}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, tools, "llama3", opts, true);

    REQUIRE(body["stream"] == true);
    REQUIRE(body["tools"].size() == 1);
    REQUIRE(body["tools"][0]["type"] == "function");
    REQUIRE(body["tools"][0]["function"]["name"] == "get_weather");
    REQUIRE(body["tools"][0]["function"]["description"] == "Get weather for a city");
    REQUIRE(body["tools"][0]["function"]["parameters"]["type"] == "object");
    REQUIRE(body["tools"][0]["function"]["parameters"]["properties"]["city"]["type"] == "string");
}

TEST_CASE("OllamaProvider build_request_body handles assistant with tool_calls", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    std::vector<ToolCall> calls = {
        ToolCall{"call_123", "get_weather", json::parse(R"({"city":"Paris"})")}
    };
    std::vector<Message> messages = {
        Message{Role::Assistant, "Let me check", std::nullopt, calls, std::nullopt}
    };
    ChatOptions opts;

    auto body = provider.build_request_body(messages, {}, "llama3", opts, false);

    REQUIRE(body["messages"][0]["role"] == "assistant");
    REQUIRE(body["messages"][0]["content"] == "Let me check");
    REQUIRE(body["messages"][0]["tool_calls"].size() == 1);
    REQUIRE(body["messages"][0]["tool_calls"][0]["type"] == "function");
    REQUIRE(body["messages"][0]["tool_calls"][0]["function"]["index"] == 0);
    REQUIRE(body["messages"][0]["tool_calls"][0]["function"]["name"] == "get_weather");
    // Arguments should be serialized as a string
    REQUIRE(body["messages"][0]["tool_calls"][0]["function"]["arguments"].is_string());
    auto parsed_args = json::parse(body["messages"][0]["tool_calls"][0]["function"]["arguments"].get<std::string>());
    REQUIRE(parsed_args["city"] == "Paris");
}

TEST_CASE("OllamaProvider parse_response extracts content and tool_calls", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto response_json = json::parse(R"({
        "model": "llama3",
        "message": {
            "role": "assistant",
            "content": "Hello!",
            "tool_calls": [
                {"type": "function", "function": {"name": "get_weather", "arguments": {"city": "NYC"}}}
            ]
        },
        "done": true,
        "done_reason": "stop",
        "prompt_eval_count": 10,
        "eval_count": 5
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello!");
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].name == "get_weather");
    REQUIRE(result.value().tool_calls[0].arguments["city"] == "NYC");
    REQUIRE(result.value().stop_reason == "stop");
    REQUIRE(result.value().usage.input_tokens == 10);
    REQUIRE(result.value().usage.output_tokens == 5);
}

TEST_CASE("OllamaProvider parse_response handles error responses", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto response_json = json::parse(R"({"error": "model not found"})");

    auto result = provider.parse_response(response_json);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("OllamaProvider parse_response handles tool_calls with string arguments", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto response_json = json::parse(R"({
        "model": "llama3",
        "message": {
            "role": "assistant",
            "content": "",
            "tool_calls": [
                {"type": "function", "function": {"name": "search", "arguments": "{\"query\": \"test\"}"}}
            ]
        },
        "done": true,
        "done_reason": "stop",
        "prompt_eval_count": 5,
        "eval_count": 3
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].name == "search");
    // String arguments should be parsed into a JSON object
    REQUIRE(result.value().tool_calls[0].arguments["query"] == "test");
}

TEST_CASE("OllamaProvider parse_response infers stop_reason from tool_calls", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    OllamaProvider provider(cfg);

    auto response_json = json::parse(R"({
        "model": "llama3",
        "message": {
            "role": "assistant",
            "content": "",
            "tool_calls": [
                {"type": "function", "function": {"name": "read_file", "arguments": {"path": "/tmp/a.txt"}}}
            ]
        },
        "done": true,
        "prompt_eval_count": 8,
        "eval_count": 2
    })");

    auto result = provider.parse_response(response_json);
    REQUIRE(result.ok());
    // No done_reason in response, but tool_calls present → should infer "tool_calls"
    REQUIRE(result.value().stop_reason == "tool_calls");
}

TEST_CASE("OllamaProvider list_models returns empty on connection failure", "[provider][ollama]") {
    OllamaProvider::Config cfg;
    cfg.base_url = "http://127.0.0.1:1";  // Invalid port, will fail
    OllamaProvider provider(cfg);

    auto models = provider.list_models();
    REQUIRE(models.empty());
}
