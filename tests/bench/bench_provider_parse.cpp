// tests/bench/bench_provider_parse.cpp
// Performance benchmarks for provider response JSON parsing
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST_CASE("Benchmark: OpenAI response JSON parsing", "[benchmark][provider]") {
    std::string openai_response = R"({
        "id": "chatcmpl-123",
        "object": "chat.completion",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "Hello! How can I help?",
                "tool_calls": []
            },
            "finish_reason": "stop"
        }],
        "usage": {"prompt_tokens": 10, "completion_tokens": 8, "total_tokens": 18}
    })";

    BENCHMARK("parse_openai_json") {
        return json::parse(openai_response);
    };
}
