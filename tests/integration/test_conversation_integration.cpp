// tests/test_conversation_integration.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "conversation/SqliteConversationStore.h"
#include "memory/HolographicMemory.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "base/Types.h"
#include <cstdio>
#include <filesystem>
#include <random>

using namespace ea;
using namespace ea::agent;
using namespace ea::conversation;
using namespace ea::memory;

// Minimal mock provider that echoes user input
class EchoProvider : public ea::IProvider {
public:
    std::string name() const override { return "echo"; }
    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        // Echo last user message
        std::string content;
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
            if (it->role == Role::User) { content = it->content; break; }
        }
        LLMResponse resp;
        resp.content = "Echo: " + content;
        resp.stop_reason = "stop";
        return resp;
    }
    Result<void> stream_chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::not_found("not implemented");
    }
    std::vector<std::string> list_models() const override { return {"echo"}; }
    provider::ProviderCapabilities capabilities() const override { return {false, false, false, false, false}; }
};

struct ConvIntegrationFixture {
    std::string db_path;
    SqliteConversationStore conv_store;
    EchoProvider provider;
    tool::ToolRegistry registry;
    HolographicMemory mem_backend;
    MemoryManager memory;

    ConvIntegrationFixture()
        : db_path(make_temp_path("_conv_int.db")),
          conv_store(SqliteConversationStore::Config{db_path, false}),
          mem_backend(HolographicMemoryConfig{":memory:", false}),
          memory(std::make_unique<HolographicMemory>(HolographicMemoryConfig{":memory:", false})) {
        mem_backend.open();
        memory.open();
        conv_store.open();
    }
    ~ConvIntegrationFixture() {
        conv_store.close();
        std::remove(db_path.c_str());
    }

private:
    static std::string make_temp_path(const std::string& suffix) {
        static std::atomic<unsigned> counter{0};
        auto dir = std::filesystem::temp_directory_path();
        std::random_device rd;
        unsigned val = rd() + counter.fetch_add(1);
        auto p = dir / ("ea_test_" + std::to_string(val) + suffix);
        return p.string();
    }
};

TEST_CASE("AgentLoop auto-persists messages", "[agent][conversation]") {
    ConvIntegrationFixture f;

    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        static_cast<ContextCompressor*>(nullptr),
        static_cast<IMemoryStrategy*>(nullptr),
        &f.conv_store
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());

    // Verify conversation was created and messages persisted
    REQUIRE_FALSE(loop.conversation_id().empty());

    auto msgs = f.conv_store.load(loop.conversation_id());
    REQUIRE(msgs.ok());
    // Should have at least user + assistant messages
    REQUIRE(msgs.value().size() >= 2);
    REQUIRE(msgs.value()[0].role == Role::User);
    REQUIRE(msgs.value()[0].content == "Hello");
}

TEST_CASE("AgentLoop restore_conversation", "[agent][conversation]") {
    ConvIntegrationFixture f;

    // First: create a conversation with some messages
    auto conv_id = f.conv_store.create().value();
    f.conv_store.append(conv_id, {Role::User, "Previous message", std::nullopt, std::nullopt, std::nullopt});
    f.conv_store.append(conv_id, {Role::Assistant, "Previous reply", std::nullopt, std::nullopt, std::nullopt});

    // Second: restore into a new AgentLoop
    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        static_cast<ContextCompressor*>(nullptr),
        static_cast<IMemoryStrategy*>(nullptr),
        &f.conv_store
    );

    auto loaded = f.conv_store.load(conv_id);
    REQUIRE(loaded.ok());
    loop.restore_conversation(conv_id, std::move(loaded.value()));

    // Verify history is restored
    const auto& history = loop.history();
    REQUIRE(history.size() == 2);
    REQUIRE(history[0].content == "Previous message");
    REQUIRE(history[1].content == "Previous reply");

    // Run a new message — should append to existing conversation
    auto result = loop.run("New message");
    REQUIRE(result.ok());
    REQUIRE(loop.conversation_id() == conv_id);

    // Verify all messages persisted (2 restored + new ones)
    auto all_msgs = f.conv_store.load(conv_id);
    REQUIRE(all_msgs.ok());
    REQUIRE(all_msgs.value().size() >= 4);  // 2 restored + user + assistant
}

TEST_CASE("AgentLoop without conv_store works normally", "[agent][conversation]") {
    EchoProvider provider;
    tool::ToolRegistry registry;
    HolographicMemory mem_backend(HolographicMemoryConfig{":memory:", false});
    mem_backend.open();
    MemoryManager memory(std::make_unique<HolographicMemory>(HolographicMemoryConfig{":memory:", false}));
    // Need to open the backend inside MemoryManager too
    memory.open();

    AgentLoop loop(
        &provider, &registry, &memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        static_cast<ContextCompressor*>(nullptr),
        static_cast<IMemoryStrategy*>(nullptr),
        static_cast<conversation::IConversationStore*>(nullptr)
    );

    auto result = loop.run("Hello");
    REQUIRE(result.ok());
    REQUIRE(loop.conversation_id().empty());  // No conversation ID
}

TEST_CASE("AgentLoop clear_history resets conversation_id", "[agent][conversation]") {
    ConvIntegrationFixture f;

    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        static_cast<ContextCompressor*>(nullptr),
        static_cast<IMemoryStrategy*>(nullptr),
        &f.conv_store
    );

    loop.run("First");
    REQUIRE_FALSE(loop.conversation_id().empty());
    std::string first_id = loop.conversation_id();

    loop.clear_history();
    REQUIRE(loop.conversation_id().empty());

    // Next run should create a new conversation
    loop.run("Second");
    REQUIRE_FALSE(loop.conversation_id().empty());
    REQUIRE(loop.conversation_id() != first_id);
}

TEST_CASE("AgentLoop persists every user message across history pruning",
          "[agent][conversation]") {
    ConvIntegrationFixture f;

    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        static_cast<ContextCompressor*>(nullptr),
        static_cast<IMemoryStrategy*>(nullptr),
        &f.conv_store
    );

    constexpr int kTurns = 20;
    for (int i = 0; i < kTurns; ++i) {
        auto result = loop.run("user-" + std::to_string(i));
        REQUIRE(result.ok());
    }

    auto msgs = f.conv_store.load(loop.conversation_id());
    REQUIRE(msgs.ok());
    REQUIRE(msgs.value().size() == static_cast<size_t>(kTurns * 2));

    for (int i = 0; i < kTurns; ++i) {
        REQUIRE(msgs.value()[static_cast<size_t>(i * 2)].role == Role::User);
        REQUIRE(msgs.value()[static_cast<size_t>(i * 2)].content
                == "user-" + std::to_string(i));
        REQUIRE(msgs.value()[static_cast<size_t>(i * 2 + 1)].role == Role::Assistant);
    }
}

TEST_CASE("AgentLoop compress_context archives and replaces live context",
          "[agent][conversation]") {
    ConvIntegrationFixture f;

    auto conv_id = f.conv_store.create().value();
    for (int i = 0; i < 10; ++i) {
        f.conv_store.append(conv_id, {Role::User, "Question " + std::to_string(i),
                                      std::nullopt, std::nullopt, std::nullopt});
        f.conv_store.append(conv_id, {Role::Assistant, "Answer " + std::to_string(i),
                                      std::nullopt, std::nullopt, std::nullopt});
    }

    ea::agent::CompressionConfig comp_cfg;
    comp_cfg.context_length = 20;
    comp_cfg.protect_first_n = 0;
    comp_cfg.protect_last_n = 2;
    ea::agent::ContextCompressor compressor(&f.provider, comp_cfg);

    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, true, false, true},
        [](const std::string&) {},
        AgentLoop::StreamFn(nullptr),
        static_cast<security::SecurityPolicy*>(nullptr),
        static_cast<security::IApprovalHandler*>(nullptr),
        &compressor,
        static_cast<IMemoryStrategy*>(nullptr),
        &f.conv_store
    );

    auto loaded = f.conv_store.load(conv_id);
    REQUIRE(loaded.ok());
    loop.restore_conversation(conv_id, std::move(loaded.value()));

    auto result = loop.compress_context();
    REQUIRE(result.ok());
    REQUIRE(result.value().compressed);
    REQUIRE(result.value().after < result.value().before);

    auto active = f.conv_store.load(conv_id);
    REQUIRE(active.ok());
    REQUIRE(active.value().size() == static_cast<size_t>(result.value().after));

    auto all = f.conv_store.load_all(conv_id);
    REQUIRE(all.ok());
    REQUIRE(all.value().size() > active.value().size());
}
