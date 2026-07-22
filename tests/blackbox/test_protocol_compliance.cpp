// tests/blackbox/test_protocol_compliance.cpp
// Protocol compliance tests — verify that provider implementations correctly
// parse real API response formats (OpenAI, Anthropic, Ollama).
// Black-box: uses only parse_response() with fixture JSON, no internal knowledge.
#include <catch2/catch_test_macros.hpp>
#include "provider/OpenAIProvider.h"
#include "provider/AnthropicProvider.h"
#include "provider/OllamaProvider.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include "nlohmann/json.hpp"

using namespace ea;
using namespace ea::provider;
using json = nlohmann::json;

namespace {

OpenAIProvider make_openai() {
    OpenAIProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test";
    return OpenAIProvider(cfg);
}

AnthropicProvider make_anthropic() {
    AnthropicProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    cfg.api_key = "test";
    return AnthropicProvider(cfg);
}

OllamaProvider make_ollama() {
    OllamaProvider::Config cfg;
    cfg.base_url = "http://localhost:1";
    return OllamaProvider(cfg);
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// OpenAI Protocol Compliance
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Protocol: OpenAI standard chat response", "[protocol][blackbox]") {
    auto provider = make_openai();

    // Real OpenAI response format
    json body = R"({
        "id": "chatcmpl-abc123",
        "object": "chat.completion",
        "created": 1677858242,
        "model": "gpt-4",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "Hello! How can I help you today?"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 10,
            "completion_tokens": 8,
            "total_tokens": 18
        }
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello! How can I help you today?");
    REQUIRE(result.value().stop_reason == "stop");
    REQUIRE(result.value().usage.input_tokens == 10);
    REQUIRE(result.value().usage.output_tokens == 8);
    REQUIRE(result.value().tool_calls.empty());
}

TEST_CASE("Protocol: OpenAI tool_calls response", "[protocol][blackbox]") {
    auto provider = make_openai();

    json body = R"({
        "id": "chatcmpl-tool123",
        "object": "chat.completion",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": null,
                "tool_calls": [{
                    "id": "call_abc",
                    "type": "function",
                    "function": {
                        "name": "get_weather",
                        "arguments": "{\"location\": \"San Francisco\"}"
                    }
                }]
            },
            "finish_reason": "tool_calls"
        }]
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().stop_reason == "tool_calls");
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].id == "call_abc");
    REQUIRE(result.value().tool_calls[0].name == "get_weather");
    REQUIRE(result.value().tool_calls[0].arguments["location"] == "San Francisco");
}

TEST_CASE("Protocol: OpenAI error response", "[protocol][blackbox]") {
    auto provider = make_openai();

    json body = R"({
        "error": {
            "message": "Rate limit exceeded",
            "type": "rate_limit_error",
            "code": "rate_limit_exceeded"
        }
    })"_json;

    // OpenAI error responses still have "choices" missing → parse error
    auto result = provider.parse_response(body);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Protocol: OpenAI empty choices returns error", "[protocol][blackbox]") {
    auto provider = make_openai();

    json body = R"({"choices": []})"_json;
    auto result = provider.parse_response(body);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("Protocol: OpenAI content with special characters", "[protocol][blackbox]") {
    auto provider = make_openai();

    json body = R"({
        "choices": [{
            "message": {"role": "assistant", "content": "Line1\nLine2\tTabbed"},
            "finish_reason": "stop"
        }]
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content.find("Line1") != std::string::npos);
    REQUIRE(result.value().content.find("Line2") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Anthropic Protocol Compliance
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Protocol: Anthropic standard response", "[protocol][blackbox]") {
    auto provider = make_anthropic();

    json body = R"({
        "id": "msg_abc123",
        "type": "message",
        "role": "assistant",
        "content": [{"type": "text", "text": "Hello from Claude!"}],
        "model": "claude-3-opus-20240229",
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 15,
            "output_tokens": 10
        }
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello from Claude!");
    REQUIRE(result.value().stop_reason == "end_turn");
    REQUIRE(result.value().usage.input_tokens == 15);
    REQUIRE(result.value().usage.output_tokens == 10);
}

TEST_CASE("Protocol: Anthropic tool_use response", "[protocol][blackbox]") {
    auto provider = make_anthropic();

    json body = R"({
        "id": "msg_tool123",
        "type": "message",
        "role": "assistant",
        "content": [
            {"type": "text", "text": "Let me check the weather."},
            {"type": "tool_use", "id": "toolu_abc", "name": "get_weather", "input": {"location": "Tokyo"}}
        ],
        "model": "claude-3-opus-20240229",
        "stop_reason": "tool_use"
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Let me check the weather.");
    REQUIRE(result.value().stop_reason == "tool_use");
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].id == "toolu_abc");
    REQUIRE(result.value().tool_calls[0].name == "get_weather");
    REQUIRE(result.value().tool_calls[0].arguments["location"] == "Tokyo");
}

TEST_CASE("Protocol: Anthropic error response", "[protocol][blackbox]") {
    auto provider = make_anthropic();

    json body = R"({
        "type": "error",
        "error": {
            "type": "overloaded_error",
            "message": "Overloaded"
        }
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().message.find("Overloaded") != std::string::npos);
}

TEST_CASE("Protocol: Anthropic cache usage tokens", "[protocol][blackbox]") {
    auto provider = make_anthropic();

    json body = R"({
        "id": "msg_cache",
        "type": "message",
        "role": "assistant",
        "content": [{"type": "text", "text": "Cached"}],
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 100,
            "output_tokens": 10,
            "cache_read_input_tokens": 80,
            "cache_creation_input_tokens": 20
        }
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().usage.cache_read_tokens == 80);
    REQUIRE(result.value().usage.cache_write_tokens == 20);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ollama Protocol Compliance
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Protocol: Ollama standard response", "[protocol][blackbox]") {
    auto provider = make_ollama();

    json body = R"({
        "model": "llama3",
        "created_at": "2024-01-01T00:00:00Z",
        "message": {"role": "assistant", "content": "Hello from Ollama!"},
        "done": true,
        "done_reason": "stop",
        "prompt_eval_count": 10,
        "eval_count": 6
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content == "Hello from Ollama!");
    REQUIRE(result.value().stop_reason == "stop");
    REQUIRE(result.value().usage.input_tokens == 10);
    REQUIRE(result.value().usage.output_tokens == 6);
}

TEST_CASE("Protocol: Ollama tool_calls response", "[protocol][blackbox]") {
    auto provider = make_ollama();

    json body = R"({
        "model": "llama3",
        "message": {
            "role": "assistant",
            "content": "",
            "tool_calls": [{
                "function": {
                    "name": "search",
                    "arguments": {"query": "test"}
                }
            }]
        },
        "done": true,
        "done_reason": "stop"
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().tool_calls.size() == 1);
    REQUIRE(result.value().tool_calls[0].name == "search");
}

TEST_CASE("Protocol: Ollama error response (string)", "[protocol][blackbox]") {
    auto provider = make_ollama();

    json body = R"({"error": "model not found"})"_json;
    auto result = provider.parse_response(body);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().message.find("model not found") != std::string::npos);
}

TEST_CASE("Protocol: Ollama error response (object)", "[protocol][blackbox]") {
    auto provider = make_ollama();

    json body = R"({"error": {"message": "OOM", "type": "resource_error"}})"_json;
    auto result = provider.parse_response(body);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().message.find("OOM") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-Protocol Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Protocol: OpenAI null content handled", "[protocol][blackbox]") {
    auto provider = make_openai();

    json body = R"({
        "choices": [{
            "message": {"role": "assistant", "content": null},
            "finish_reason": "stop"
        }]
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    // null content should result in empty string
    REQUIRE(result.value().content.empty());
}

TEST_CASE("Protocol: Anthropic empty content array", "[protocol][blackbox]") {
    auto provider = make_anthropic();

    json body = R"({
        "id": "msg_empty",
        "type": "message",
        "role": "assistant",
        "content": [],
        "stop_reason": "end_turn"
    })"_json;

    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    REQUIRE(result.value().content.empty());
}

TEST_CASE("Protocol: Ollama missing message field", "[protocol][blackbox]") {
    auto provider = make_ollama();

    json body = R"({"model": "llama3", "done": true})"_json;
    auto result = provider.parse_response(body);
    REQUIRE(result.ok());
    // Missing message → empty response but not error
    REQUIRE(result.value().content.empty());
}
