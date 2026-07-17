// tests/test_streaming.cpp — Streaming integration tests
#include <catch2/catch_test_macros.hpp>
#include <queue>
#include "agent/AgentLoop.h"
#include "agent/TurnContext.h"
#include "agent/steps/CallProviderStep.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "config/Config.h"
#include "common/io/FileSystem.h"

using namespace ea;
using namespace ea::agent;
using ToolRegistry = tool::ToolRegistry;

// Mock provider that supports streaming
class MockStreamProvider : public IProvider {
public:
    std::string name() const override { return "mock-stream"; }
    std::vector<std::string> list_models() const override { return {"mock-stream-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, true, false, false};  // streaming = true
    }

    // Queue chunks to be emitted by stream_chat
    void enqueue_chunks(std::vector<StreamChunk> chunks) {
        chunk_queues_.push(std::move(chunks));
    }

    // Queue responses for chat (used in degraded mode tests)
    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        if (responses_.empty()) return Error::net("no mock responses");
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)> on_chunk,
                              const ChatOptions&) override {
        if (chunk_queues_.empty()) return Error::net("no mock chunks");
        auto chunks = std::move(chunk_queues_.front());
        chunk_queues_.pop();
        for (const auto& chunk : chunks) {
            on_chunk(chunk);
        }
        return {};
    }

private:
    std::queue<std::vector<StreamChunk>> chunk_queues_;
    std::queue<LLMResponse> responses_;
};

// Mock provider that does NOT support streaming
class MockNonStreamProvider : public IProvider {
public:
    std::string name() const override { return "mock-nonstream"; }
    std::vector<std::string> list_models() const override { return {"mock-nonstream-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, false, false, false, false};  // streaming = false
    }

    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        if (responses_.empty()) return Error::net("no mock responses");
        auto r = std::move(responses_.front());
        responses_.pop();
        return r;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

private:
    std::queue<LLMResponse> responses_;
};

TEST_CASE("StreamFn content forwarding", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    // Queue: two content chunks + done
    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Hello", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, " world", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(received.size() >= 2);
    REQUIRE(received[0].type == StreamChunk::Type::Content);
    REQUIRE(received[0].data == "Hello");
    REQUIRE(received[1].type == StreamChunk::Type::Content);
    REQUIRE(received[1].data == " world");
}

TEST_CASE("Streaming accumulates complete LLMResponse", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Part1 ", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, "Part2", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::string accumulated;
    auto stream_fn = [&](const StreamChunk& chunk) {
        if (chunk.type == StreamChunk::Type::Content) {
            accumulated += chunk.data;
        }
    };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // Verify accumulated content from all Content chunks
    REQUIRE(accumulated == "Part1 Part2");
}

TEST_CASE("Streaming tool call accumulation", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    ToolCall tc;
    tc.id = "call_1";
    tc.name = "shell";
    tc.arguments = json::object();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::ToolCallBegin, "", tc, std::nullopt},
        StreamChunk{StreamChunk::Type::ToolCallEnd, "", tc, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // Should have received ToolCallBegin and ToolCallEnd
    bool has_begin = false, has_end = false;
    for (const auto& c : received) {
        if (c.type == StreamChunk::Type::ToolCallBegin) has_begin = true;
        if (c.type == StreamChunk::Type::ToolCallEnd) has_end = true;
    }
    REQUIRE(has_begin);
    REQUIRE(has_end);
}

TEST_CASE("Degraded streaming with non-streaming provider", "[streaming]") {
    auto provider = std::make_shared<MockNonStreamProvider>();

    LLMResponse resp;
    resp.content = "Fallback response";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::vector<StreamChunk> received;
    auto stream_fn = [&](const StreamChunk& chunk) { received.push_back(chunk); };

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [](const std::string&) {}, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // Should have received simulated Content + Done chunks
    REQUIRE(received.size() >= 2);
    REQUIRE(received[0].type == StreamChunk::Type::Content);
    REQUIRE(received[0].data == "Fallback response");
    // Last should be Done
    REQUIRE(received.back().type == StreamChunk::Type::Done);
}

TEST_CASE("Non-streaming path unchanged when no StreamFn", "[streaming]") {
    LLMResponse resp;
    resp.content = "Non-stream response";
    resp.stop_reason = "stop";

    auto non_stream = std::make_shared<MockNonStreamProvider>();
    non_stream->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    // No stream_fn — should use OutputFn
    AgentLoop loop(non_stream.get(), &registry, nullptr,
                   AgentLoop::Config{90, 65536, 100, true, false},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Non-stream response");
}

TEST_CASE("Stream interruption via StreamInterrupted", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    // Queue chunks — the test will interrupt after first chunk
    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Before interrupt", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Content, "After interrupt", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    std::atomic<bool> interrupted{false};
    int chunk_count = 0;

    auto stream_fn = [&](const StreamChunk& chunk) {
        chunk_count++;
        if (chunk_count == 1) {
            // Simulate interrupt after first chunk
            interrupted.store(true);
        }
    };

    // We need to test CallProviderStep directly since AgentLoop::interrupt()
    // sets its own atomic. Use TurnContext directly.
    std::vector<Message> messages;
    TurnContext ctx(messages, interrupted);
    ctx.provider = provider.get();
    ctx.stream_callback = stream_fn;
    ctx.system_prompt = "test";

    CallProviderStep step;
    auto result = step.execute(ctx);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::Timeout);
}

TEST_CASE("AgentLoop streaming skips OutputFn", "[streaming]") {
    auto provider = std::make_shared<MockStreamProvider>();

    provider->enqueue_chunks({
        StreamChunk{StreamChunk::Type::Content, "Streamed text", std::nullopt, std::nullopt},
        StreamChunk{StreamChunk::Type::Done, "", std::nullopt, std::nullopt}
    });

    ToolRegistry registry;
    std::string output;
    auto stream_fn = [](const StreamChunk&) {};

    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{}, [&](const std::string& t) { output = t; }, stream_fn);

    auto result = loop.run("test");
    REQUIRE(result.ok());
    // OutputFn should NOT have been called in streaming mode
    REQUIRE(output.empty());
}

TEST_CASE("AgentLoop non-streaming uses OutputFn", "[streaming]") {
    auto provider = std::make_shared<MockNonStreamProvider>();

    LLMResponse resp;
    resp.content = "Non-stream output";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;

    // Config with stream=false, no StreamFn
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{90, 65536, 100, true, false},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(output == "Non-stream output");
}

TEST_CASE("Config stream field parsing", "[streaming]") {
    std::string tmp = "/tmp/ea_test_stream_config.toml";
    ea::fs::write_file(tmp, R"(
[agent]
stream = false
)");

    auto cfg = ea::config::load(tmp);
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().agent.stream == false);

    // Default should be true
    auto cfg2 = ea::config::load("/nonexistent/config.toml");
    REQUIRE(cfg2.ok());
    REQUIRE(cfg2.value().agent.stream == true);

    ea::fs::remove(tmp);
}
