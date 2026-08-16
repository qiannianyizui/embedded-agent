// tests/unit/test_memory_extractor.cpp
// Tests for the background batch memory extractor.
#include <catch2/catch_test_macros.hpp>
#include "memory/MemoryExtractor.h"
#include "memory/HolographicMemory.h"
#include "conversation/SqliteConversationStore.h"
#include "provider/IProvider.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace ea;
using namespace ea::memory;
using namespace ea::conversation;

namespace {

class ExtractorTestProvider : public IProvider {
public:
    std::string name() const override { return "extractor-test"; }
    std::vector<std::string> list_models() const override { return {"test-model"}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        chat_count_++;
        last_messages_ = msgs;
        if (fail_next_) {
            fail_next_ = false;
            return Error::net("provider error");
        }
        LLMResponse resp;
        resp.content = next_response_;
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int chat_count() const { return chat_count_; }
    void set_fail_next(bool f) { fail_next_ = f; }
    void set_response(std::string r) { next_response_ = std::move(r); }
    const std::vector<Message>& last_messages() const { return last_messages_; }

private:
    int chat_count_ = 0;
    bool fail_next_ = false;
    std::string next_response_ = "- user prefers dark mode\n- project uses CMake";
    std::vector<Message> last_messages_;
};

struct TestEnv {
    std::string dir;
    std::string db_path;
    std::unique_ptr<SqliteConversationStore> store;
    std::unique_ptr<HolographicMemory> memory;
    std::shared_ptr<ExtractorTestProvider> provider;

    TestEnv() {
        dir = (std::filesystem::temp_directory_path() / "ea_memory_extractor_test").string();
        std::filesystem::create_directories(dir);
        db_path = dir + "/conversations.db";
        store = std::make_unique<SqliteConversationStore>(
            SqliteConversationStore::Config{db_path});
        REQUIRE(store->open().ok());
        memory = std::make_unique<HolographicMemory>(
            HolographicMemoryConfig{":memory:", false});
        REQUIRE(memory->open().ok());
        provider = std::make_shared<ExtractorTestProvider>();
    }

    ~TestEnv() {
        if (store) store->close();
        std::filesystem::remove_all(dir);
    }

    std::string make_conversation() {
        auto id = store->create();
        REQUIRE(id.ok());
        return id.value();
    }

    void append(const std::string& cid, Role role, const std::string& content) {
        Message msg{role, content, std::nullopt, std::nullopt, std::nullopt};
        REQUIRE(store->append(cid, msg).ok());
    }
};

bool wait_for(const std::function<bool()>& pred, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

int long_term_count(HolographicMemory& mem) {
    auto facts = mem.list_facts("long_term");
    if (!facts.ok()) return -1;
    return static_cast<int>(facts.value().size());
}

}  // anonymous namespace

TEST_CASE("MemoryExtractor extracts facts on enqueue", "[memory][extractor]") {
    TestEnv env;
    env.append(env.make_conversation(), Role::User, "I prefer dark mode");
    env.append(env.make_conversation(), Role::Assistant, "Noted");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    extractor.enqueue(env.store->list(10, 0).value()[0].id);

    REQUIRE(wait_for([&] { return env.provider->chat_count() == 1; }));
    REQUIRE(wait_for([&] { return long_term_count(*env.memory) == 2; }));
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor extracts only incremental messages", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::User, "I prefer dark mode");
    env.append(cid, Role::Assistant, "Noted");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    extractor.enqueue(cid);
    REQUIRE(wait_for([&] { return env.provider->chat_count() == 1; }));

    // Second round: only the new message should be extracted.
    env.append(cid, Role::User, "Also prefer light mode sometimes");
    env.append(cid, Role::Assistant, "Got it");
    env.provider->set_response("- user toggles between themes");

    extractor.enqueue(cid);
    REQUIRE(wait_for([&] { return env.provider->chat_count() == 2; }));
    REQUIRE(wait_for([&] { return long_term_count(*env.memory) == 3; }));
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor LLM failure does not advance watermark", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::User, "I prefer dark mode");
    env.append(cid, Role::Assistant, "Noted");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    env.provider->set_fail_next(true);
    extractor.enqueue(cid);
    REQUIRE(wait_for([&] { return env.provider->chat_count() == 1; }));
    REQUIRE(long_term_count(*env.memory) == 0);

    // Re-trigger: the failed batch must be retried, nothing lost.
    extractor.enqueue(cid);
    REQUIRE(wait_for([&] { return env.provider->chat_count() == 2; }));
    REQUIRE(wait_for([&] { return long_term_count(*env.memory) == 2; }));
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor deduplicates queued conversations", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::User, "I prefer dark mode");
    env.append(cid, Role::Assistant, "Noted");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    extractor.enqueue(cid);
    extractor.enqueue(cid);
    extractor.enqueue(cid);

    REQUIRE(wait_for([&] { return env.provider->chat_count() == 1; }));
    // Give the worker time to (incorrectly) process duplicates if any.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(env.provider->chat_count() == 1);
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor skips conversations without user messages", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::Assistant, "Hello, how can I help?");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    extractor.enqueue(cid);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(env.provider->chat_count() == 0);
    REQUIRE(long_term_count(*env.memory) == 0);
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor startup catch-up extracts unextracted conversations", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::User, "I prefer dark mode");
    env.append(cid, Role::Assistant, "Noted");

    MemoryExtractor::Config cfg;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();  // no enqueue — catch-up must find it

    REQUIRE(wait_for([&] { return env.provider->chat_count() == 1; }));
    REQUIRE(wait_for([&] { return long_term_count(*env.memory) == 2; }));
    extractor.shutdown();
}

TEST_CASE("MemoryExtractor disabled does not start a worker", "[memory][extractor]") {
    TestEnv env;
    auto cid = env.make_conversation();
    env.append(cid, Role::User, "I prefer dark mode");

    MemoryExtractor::Config cfg;
    cfg.enable = false;
    MemoryExtractor extractor(env.provider, env.memory.get(), env.db_path, cfg);
    extractor.start();

    extractor.enqueue(cid);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(env.provider->chat_count() == 0);
    extractor.shutdown();
}
