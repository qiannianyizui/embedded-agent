// tests/test_conversation_integration.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "conversation/SqliteConversationStore.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "common/base/Types.h"
#include <cstdio>

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
    InMemoryBackend mem_backend;
    MemoryManager memory;

    ConvIntegrationFixture()
        : db_path(std::tmpnam(nullptr) + std::string("_conv_int.db")),
          conv_store(SqliteConversationStore::Config{db_path, false}),
          memory(std::make_unique<InMemoryBackend>()) {
        conv_store.open();
    }
    ~ConvIntegrationFixture() {
        conv_store.close();
        std::remove(db_path.c_str());
    }
};

TEST_CASE("AgentLoop auto-persists messages", "[agent][conversation]") {
    ConvIntegrationFixture f;

    AgentLoop loop(
        &f.provider, &f.registry, &f.memory,
        AgentLoop::Config{10, 65536, 100, true, false, true},
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
        AgentLoop::Config{10, 65536, 100, true, false, true},
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
    InMemoryBackend mem_backend;
    MemoryManager memory(std::make_unique<InMemoryBackend>());

    AgentLoop loop(
        &provider, &registry, &memory,
        AgentLoop::Config{10, 65536, 100, true, false, true},
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
        AgentLoop::Config{10, 65536, 100, true, false, true},
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
