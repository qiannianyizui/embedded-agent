// tests/test_conversation_store.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "conversation/SqliteConversationStore.h"
#include "base/Types.h"
#include <cstdio>
#include <filesystem>
#include <random>
#include <atomic>

using namespace ea;
using namespace ea::conversation;

// Helper: create a temp DB path, auto-cleanup
struct TempConvStore {
    std::string path;
    SqliteConversationStore store;

    TempConvStore() : path(make_temp_path("_conv.db")),
                      store(SqliteConversationStore::Config{path, false}) {
        auto r = store.open();
        REQUIRE(r.ok());
    }
    ~TempConvStore() {
        store.close();
        std::remove(path.c_str());
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

TEST_CASE("ConversationStore create returns valid ID", "[conversation]") {
    TempConvStore t;
    auto r = t.store.create("test-model");
    REQUIRE(r.ok());
    REQUIRE(r.value().substr(0, 5) == "conv_");
    REQUIRE(r.value().size() == 13);  // "conv_" + 8 hex
}

TEST_CASE("ConversationStore append and load", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    Message user_msg{Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt};
    Message asst_msg{Role::Assistant, "Hi there!", std::nullopt, std::nullopt, std::nullopt};

    REQUIRE(t.store.append(id, user_msg).ok());
    REQUIRE(t.store.append(id, asst_msg).ok());

    auto loaded = t.store.load(id);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().size() == 2);
    REQUIRE(loaded.value()[0].role == Role::User);
    REQUIRE(loaded.value()[0].content == "Hello");
    REQUIRE(loaded.value()[1].role == Role::Assistant);
    REQUIRE(loaded.value()[1].content == "Hi there!");
}

TEST_CASE("ConversationStore load non-existent returns error", "[conversation]") {
    TempConvStore t;
    auto r = t.store.load("conv_nonexist");
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("ConversationStore title from first user message", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    Message user_msg{Role::User, "This is a very long first user message that should be truncated", std::nullopt, std::nullopt, std::nullopt};
    t.store.append(id, user_msg);

    auto meta = t.store.get_meta(id);
    REQUIRE(meta.ok());
    REQUIRE(meta.value().title.size() <= 50);
    REQUIRE(meta.value().title == user_msg.content.substr(0, 50));
}

TEST_CASE("ConversationStore message_count", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    t.store.append(id, {Role::User, "Hi", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id, {Role::Assistant, "Hello", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id, {Role::User, "How are you?", std::nullopt, std::nullopt, std::nullopt});

    auto meta = t.store.get_meta(id);
    REQUIRE(meta.ok());
    REQUIRE(meta.value().message_count == 3);
}

TEST_CASE("ConversationStore list ordered by updated_at desc", "[conversation]") {
    TempConvStore t;
    auto id1 = t.store.create().value();
    auto id2 = t.store.create().value();

    // Append to id1 first, then id2 — id2 should be most recent
    t.store.append(id1, {Role::User, "First", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id2, {Role::User, "Second", std::nullopt, std::nullopt, std::nullopt});

    auto list = t.store.list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
    REQUIRE(list.value()[0].id == id2);  // Most recent first
    REQUIRE(list.value()[1].id == id1);
}

TEST_CASE("ConversationStore remove", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();
    t.store.append(id, {Role::User, "Test", std::nullopt, std::nullopt, std::nullopt});

    REQUIRE(t.store.remove(id).ok());
    REQUIRE_FALSE(t.store.get_meta(id).ok());
}

TEST_CASE("ConversationStore remove non-existent returns false", "[conversation]") {
    TempConvStore t;
    auto r = t.store.remove("conv_nonexist");
    REQUIRE(r.ok());
    REQUIRE_FALSE(r.value());  // false = not found
}

TEST_CASE("ConversationStore archive_and_compact keeps full history", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    t.store.append(id, {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id, {Role::Assistant, "Hi there", std::nullopt, std::nullopt, std::nullopt});

    std::vector<Message> compressed;
    compressed.push_back({Role::User, "[Conversation Summary] Earlier turns",
                          std::nullopt, std::nullopt, std::nullopt});
    compressed.push_back({Role::Assistant, "Continue from summary",
                          std::nullopt, std::nullopt, std::nullopt});

    REQUIRE(t.store.archive_and_compact(id, compressed).ok());

    auto active = t.store.load(id);
    REQUIRE(active.ok());
    REQUIRE(active.value().size() == compressed.size());
    REQUIRE(active.value()[0].content == "[Conversation Summary] Earlier turns");

    auto all = t.store.load_all(id);
    REQUIRE(all.ok());
    REQUIRE(all.value().size() == 4);  // 2 archived + 2 active

    auto meta = t.store.get_meta(id);
    REQUIRE(meta.ok());
    REQUIRE(meta.value().message_count == static_cast<int>(compressed.size()));
}

TEST_CASE("ConversationStore tool_calls serialization", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    ToolCall tc{"tc_1", "shell", json{{"command", "echo hello"}}};
    Message asst_msg{Role::Assistant, "", std::nullopt, std::vector<ToolCall>{tc}, std::nullopt};
    t.store.append(id, asst_msg);

    Message tool_msg{Role::Tool, "hello\n", std::string("shell"), std::nullopt, std::string("tc_1")};
    t.store.append(id, tool_msg);

    auto loaded = t.store.load(id);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().size() == 2);

    // Check assistant message with tool_calls
    auto& a = loaded.value()[0];
    REQUIRE(a.role == Role::Assistant);
    REQUIRE(a.tool_calls.has_value());
    REQUIRE(a.tool_calls.value().size() == 1);
    REQUIRE(a.tool_calls.value()[0].id == "tc_1");
    REQUIRE(a.tool_calls.value()[0].name == "shell");
    REQUIRE(a.tool_calls.value()[0].arguments["command"] == "echo hello");

    // Check tool message with name and tool_call_id
    auto& tmsg = loaded.value()[1];
    REQUIRE(tmsg.role == Role::Tool);
    REQUIRE(tmsg.name.has_value());
    REQUIRE(tmsg.name.value() == "shell");
    REQUIRE(tmsg.tool_call_id.has_value());
    REQUIRE(tmsg.tool_call_id.value() == "tc_1");
}

TEST_CASE("ConversationStore JSONL export/import round-trip", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create("test-model").value();

    t.store.append(id, {Role::System, "You are helpful.", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id, {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt});
    t.store.append(id, {Role::Assistant, "Hi!", std::nullopt, std::nullopt, std::nullopt});

    auto exported = t.store.export_jsonl(id);
    REQUIRE(exported.ok());
    REQUIRE_FALSE(exported.value().empty());

    // Import into a new conversation
    auto new_id = t.store.import_jsonl(exported.value(), "imported-model");
    REQUIRE(new_id.ok());
    REQUIRE(new_id.value().substr(0, 5) == "conv_");
    REQUIRE(new_id.value() != id);  // Different ID

    // Verify round-trip
    auto loaded = t.store.load(new_id.value());
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().size() == 3);
    REQUIRE(loaded.value()[0].role == Role::System);
    REQUIRE(loaded.value()[0].content == "You are helpful.");
    REQUIRE(loaded.value()[1].role == Role::User);
    REQUIRE(loaded.value()[2].role == Role::Assistant);
    REQUIRE(loaded.value()[2].content == "Hi!");
}

TEST_CASE("ConversationStore JSONL import malformed", "[conversation]") {
    TempConvStore t;
    auto r = t.store.import_jsonl("not valid json\n");
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("ConversationStore JSONL import missing role", "[conversation]") {
    TempConvStore t;
    auto r = t.store.import_jsonl("{\"content\":\"hello\"}\n");
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("ConversationStore empty conversation load", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();
    auto loaded = t.store.load(id);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().empty());
}

TEST_CASE("ConversationStore model stored in metadata", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create("llama3").value();
    auto meta = t.store.get_meta(id);
    REQUIRE(meta.ok());
    REQUIRE(meta.value().model == "llama3");
}

TEST_CASE("ConversationStore list with limit and offset", "[conversation]") {
    TempConvStore t;
    // Create 5 conversations
    for (int i = 0; i < 5; ++i) {
        auto id = t.store.create().value();
        t.store.append(id, {Role::User, "Msg " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto page1 = t.store.list(2, 0);
    REQUIRE(page1.ok());
    REQUIRE(page1.value().size() == 2);

    auto page2 = t.store.list(2, 2);
    REQUIRE(page2.ok());
    REQUIRE(page2.value().size() == 2);

    // No overlap
    REQUIRE(page1.value()[0].id != page2.value()[0].id);
    REQUIRE(page1.value()[1].id != page2.value()[1].id);
}

TEST_CASE("ConversationStore special characters in content", "[conversation]") {
    TempConvStore t;
    auto id = t.store.create().value();

    std::string special = "Hello \"world\" with 'quotes' and \n newlines \t tabs \\ backslash";
    t.store.append(id, {Role::User, special, std::nullopt, std::nullopt, std::nullopt});

    auto loaded = t.store.load(id);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value()[0].content == special);
}
