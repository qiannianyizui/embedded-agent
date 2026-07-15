// Integration: AgentLoop + InMemoryBackend + MemoryManager
// Verifies memory recall is injected into system prompt and
// auto-memory stores/recalls work across turns.

#include <queue>
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"

using namespace ea;
using namespace ea::agent;
using namespace ea::memory;

// Mock Provider that captures messages for inspection
class MockProviderForMemory : public IProvider {
public:
    std::string name() const override { return "mock-memory"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void enqueue_response(LLMResponse resp) {
        responses_.push(std::move(resp));
    }

    Result<LLMResponse> chat(const std::vector<Message>& messages,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        last_messages_ = messages;
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

    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    std::queue<LLMResponse> responses_;
    std::vector<Message> last_messages_;
};

TEST_CASE("Integration: AgentLoop with InMemoryBackend prefetches memories", "[integration][agent][memory]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("user prefers dark mode", "preference", 7);
    auto mem_ptr = backend.get();

    auto provider = std::make_unique<MockProviderForMemory>();
    LLMResponse resp;
    resp.content = "I'll use dark mode for you.";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, mem_ptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("set my theme");
    REQUIRE(result.ok());
    REQUIRE(output == "I'll use dark mode for you.");

    // Verify system prompt contains memory content
    auto& msgs = provider->last_messages();
    bool found_memory = false;
    for (const auto& m : msgs) {
        if (m.role == Role::System && m.content.find("dark mode") != std::string::npos) {
            found_memory = true;
            break;
        }
    }
    REQUIRE(found_memory);
}

TEST_CASE("Integration: AgentLoop with null memory still works", "[integration][agent][memory]") {
    auto provider = std::make_unique<MockProviderForMemory>();
    LLMResponse resp;
    resp.content = "Hello!";
    resp.stop_reason = "stop";
    provider->enqueue_response(std::move(resp));

    ToolRegistry registry;
    std::string output;
    AgentLoop loop(provider.get(), &registry, nullptr,
                   AgentLoop::Config{},
                   [&](const std::string& t) { output = t; });

    auto result = loop.run("Hi");
    REQUIRE(result.ok());
    REQUIRE(output == "Hello!");
}

TEST_CASE("Integration: MemoryManager sync_turn lifecycle", "[integration][memory]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("fact about C++", "core", 7);

    MemoryManager mgr(std::move(backend));

    // Prefetch
    auto results = mgr.prefetch("C++");
    REQUIRE(results.size() >= 1);

    // Sync turn
    mgr.sync_turn("tell me about C++", "C++ is a systems programming language");

    // Next prefetch should still work
    auto results2 = mgr.prefetch("C++");
    REQUIRE(results2.size() >= 1);

    // Build memory block
    auto block = mgr.build_memory_block();
    REQUIRE_FALSE(block.empty());
}
