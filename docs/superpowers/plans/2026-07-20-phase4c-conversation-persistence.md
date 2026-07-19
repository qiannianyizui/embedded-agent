# Phase 4C: Conversation Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add conversation persistence so conversations survive process exit (CLI) and session expiry (Server), with auto-resume, manual history/resume/export/import commands, and Server API endpoints.

**Architecture:** New `ea::conversation` module with `IConversationStore` interface + `SqliteConversationStore` implementation in a separate SQLite file (`conversations.db`). AgentLoop gains an optional `IConversationStore*` pointer for auto-persist; Server mode gains `/api/conversations/*` endpoints and session restoration via `conversation_id`.

**Tech Stack:** C++17, CMake, SQLite3, nlohmann/json, Catch2, httplib

## Global Constraints

- Namespace: `ea::conversation` for all new types
- Error construction: use `Error` factory methods (`Error::db()`, `Error::not_found()`, `Error::parse()`, `Error::invalid_arg()`)
- Compile flags: use `ea_target_compile_options(target)` for project targets
- Object libraries: each module is a CMake `OBJECT` library
- Test tags: `[conversation]` for unit tests, `[agent] [conversation]` or `[server] [conversation]` for integration tests
- Persistence failures never block the main conversation flow
- `conv_store_ == nullptr` means original AgentLoop behavior, zero impact
- Conversation IDs: `"conv_"` + 8 hex chars (same pattern as SessionManager's `"sess_"`)
- Database: separate SQLite file from `memory.db`, with WAL mode
- JSONL format: one JSON object per line, each representing a `Message`
- `extra_json` column stores optional Message fields (`name`, `tool_calls`, `tool_call_id`) as JSON; `std::nullopt` fields are omitted from the JSON object

---

### Task 1: IConversationStore Interface + ConversationMeta

**Files:**
- Create: `src/conversation/IConversationStore.h`
- Create: `src/conversation/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `add_subdirectory(src/conversation)` and `$<TARGET_OBJECTS:ea-conversation>`)
- Modify: `tests/CMakeLists.txt` (add `ea-conversation` to link libraries)

**Interfaces:**
- Consumes: `ea::Result<T>` from `common/base/Result.h`, `ea::Message` from `core/Types.h`
- Produces: `ea::conversation::ConversationMeta` struct, `ea::conversation::IConversationStore` interface

- [ ] **Step 1: Create the conversation module directory**

```bash
mkdir -p src/conversation
```

- [ ] **Step 2: Write IConversationStore.h**

```cpp
// src/conversation/IConversationStore.h
#pragma once
#include "core/Types.h"
#include "common/base/Result.h"
#include <string>
#include <vector>

namespace ea::conversation {

struct ConversationMeta {
    std::string id;           // "conv_" + 8 hex chars
    std::string title;        // First user message, truncated to 50 chars
    std::string created_at;   // ISO 8601
    std::string updated_at;   // ISO 8601
    int message_count = 0;
    std::string model;        // Model used (optional)
};

class IConversationStore {
public:
    virtual ~IConversationStore() = default;

    // Create a new conversation, return its ID
    virtual Result<std::string> create(const std::string& model = "") = 0;

    // Append a message to a conversation
    virtual Result<void> append(const std::string& conversation_id,
                                const Message& msg) = 0;

    // Load all messages for a conversation
    virtual Result<std::vector<Message>> load(const std::string& conversation_id) = 0;

    // List conversations (ordered by updated_at descending)
    virtual Result<std::vector<ConversationMeta>> list(int limit = 50, int offset = 0) = 0;

    // Get metadata for a single conversation
    virtual Result<ConversationMeta> get_meta(const std::string& conversation_id) = 0;

    // Delete a conversation
    virtual Result<bool> remove(const std::string& conversation_id) = 0;

    // Export conversation as JSONL string
    virtual Result<std::string> export_jsonl(const std::string& conversation_id) = 0;

    // Import conversation from JSONL string, return new conversation ID
    virtual Result<std::string> import_jsonl(const std::string& jsonl_data,
                                              const std::string& model = "") = 0;

    // Lifecycle
    virtual Result<void> open() = 0;
    virtual Result<void> close() = 0;
};

}  // namespace ea::conversation
```

- [ ] **Step 3: Write src/conversation/CMakeLists.txt**

```cmake
add_library(ea-conversation OBJECT
    SqliteConversationStore.cpp
)
ea_target_compile_options(ea-conversation)
target_include_directories(ea-conversation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-conversation PUBLIC ea-core ea-common sqlite3)
```

Note: `SqliteConversationStore.cpp` does not exist yet (Task 2 creates it). For now, create a minimal stub so CMake doesn't fail:

```cpp
// src/conversation/SqliteConversationStore.cpp (temporary stub)
#include "SqliteConversationStore.h"
namespace ea::conversation {
SqliteConversationStore::SqliteConversationStore(Config config) : config_(std::move(config)) {}
SqliteConversationStore::~SqliteConversationStore() { if (opened_) close(); }
}
```

- [ ] **Step 4: Write SqliteConversationStore.h (declaration only, implementation in Task 2)**

```cpp
// src/conversation/SqliteConversationStore.h
#pragma once
#include "IConversationStore.h"
#include <string>
#include <memory>

struct sqlite3;

namespace ea::conversation {

class SqliteConversationStore : public IConversationStore {
public:
    struct Config {
        std::string path;       // Path to conversations.db
        bool enable_wal = true;
    };

    explicit SqliteConversationStore(Config config);
    ~SqliteConversationStore() override;

    SqliteConversationStore(const SqliteConversationStore&) = delete;
    SqliteConversationStore& operator=(const SqliteConversationStore&) = delete;

    Result<std::string> create(const std::string& model = "") override;
    Result<void> append(const std::string& conversation_id, const Message& msg) override;
    Result<std::vector<Message>> load(const std::string& conversation_id) override;
    Result<std::vector<ConversationMeta>> list(int limit = 50, int offset = 0) override;
    Result<ConversationMeta> get_meta(const std::string& conversation_id) override;
    Result<bool> remove(const std::string& conversation_id) override;
    Result<std::string> export_jsonl(const std::string& conversation_id) override;
    Result<std::string> import_jsonl(const std::string& jsonl_data,
                                      const std::string& model = "") override;
    Result<void> open() override;
    Result<void> close() override;

private:
    Result<void> create_tables();
    Result<void> update_meta_on_append(const std::string& conversation_id,
                                        const std::string& first_user_content);
    std::string role_to_string(Role role) const;
    Role string_to_role(const std::string& s) const;
    json message_to_extra_json(const Message& msg) const;
    Message row_to_message(const std::string& role, const std::string& content,
                           const std::string& extra_json) const;

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
};

}  // namespace ea::conversation
```

- [ ] **Step 5: Modify top-level CMakeLists.txt**

Add `add_subdirectory(src/conversation)` after the existing `add_subdirectory(src/memory)` line (line 93):

```cmake
add_subdirectory(src/memory)
add_subdirectory(src/conversation)   # NEW
add_subdirectory(src/provider)
```

Add `$<TARGET_OBJECTS:ea-conversation>` to the `embedded-agent-core` static library (after `ea-memory`):

```cmake
add_library(embedded-agent-core STATIC
    $<TARGET_OBJECTS:ea-common>
    $<TARGET_OBJECTS:ea-provider>
    $<TARGET_OBJECTS:ea-tool>
    $<TARGET_OBJECTS:ea-memory>
    $<TARGET_OBJECTS:ea-conversation>   # NEW
    $<TARGET_OBJECTS:ea-agent>
    $<TARGET_OBJECTS:ea-mcp>
    $<TARGET_OBJECTS:ea-platform>
    $<TARGET_OBJECTS:ea-config>
    $<TARGET_OBJECTS:ea-security>
    $<TARGET_OBJECTS:ea-server>
)
```

- [ ] **Step 6: Modify tests/CMakeLists.txt**

Add `ea-conversation` to the `target_link_libraries` for `ea-tests` (after `ea-memory`):

```cmake
target_link_libraries(ea-tests PRIVATE
    ea-common
    ea-config
    ea-memory
    ea-conversation   # NEW
    ea-provider
    ea-tool
    ea-agent
    ea-mcp
    ea-platform
    ea-security
    ea-server
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
)
```

- [ ] **Step 7: Build to verify CMake configuration**

```bash
cd build && cmake --build . -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds (stub implementation compiles).

- [ ] **Step 8: Commit**

```bash
git add src/conversation/ CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add IConversationStore interface and conversation module scaffold"
```

---

### Task 2: SqliteConversationStore Implementation

**Files:**
- Modify: `src/conversation/SqliteConversationStore.cpp` (replace stub with full implementation)

**Interfaces:**
- Consumes: `IConversationStore`, `ConversationMeta`, `ea::Message`, `ea::Result<T>`, `ea::Error`, `sqlite3`, `nlohmann/json`
- Produces: Full `SqliteConversationStore` with all CRUD + JSONL export/import

- [ ] **Step 1: Write the failing test file**

Create `tests/test_conversation_store.cpp`:

```cpp
// tests/test_conversation_store.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "conversation/SqliteConversationStore.h"
#include "core/Types.h"
#include <cstdio>

using namespace ea;
using namespace ea::conversation;

// Helper: create a temp DB path, auto-cleanup
struct TempConvStore {
    std::string path;
    SqliteConversationStore store;

    TempConvStore() : path(std::tmpnam(nullptr) + std::string("_conv.db")),
                      store(SqliteConversationStore::Config{path, false}) {
        auto r = store.open();
        REQUIRE(r.ok());
    }
    ~TempConvStore() {
        store.close();
        std::remove(path.c_str());
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
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

Add `test_conversation_store.cpp` to the `ea-tests` source list (after `test_memory_strategy.cpp`):

```cmake
add_executable(ea-tests
    ...
    test_memory_strategy.cpp
    test_conversation_store.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[conversation]" 2>&1 | tail -30
```

Expected: Tests fail because `SqliteConversationStore` methods are not implemented (stub only has constructor/destructor).

- [ ] **Step 4: Implement SqliteConversationStore.cpp**

Replace the stub with the full implementation:

```cpp
// src/conversation/SqliteConversationStore.cpp
#include "SqliteConversationStore.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include <sstream>
#include <iomanip>
#include <random>

namespace ea::conversation {

using json = nlohmann::json;

// --- Helpers ---

static std::string generate_conv_id() {
    static std::mt19937 rng{std::random_device{}()};
    std::stringstream ss;
    ss << "conv_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (rng() & 0xFF);
    }
    return ss.str();
}

static std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// --- Construction / Lifecycle ---

SqliteConversationStore::SqliteConversationStore(Config config)
    : config_(std::move(config)) {}

SqliteConversationStore::~SqliteConversationStore() {
    if (opened_) close();
}

Result<void> SqliteConversationStore::open() {
    if (opened_) return {};

    EA_DEBUG("SqliteConversationStore::open() path={}", config_.path);

    // Ensure parent directory exists
    if (config_.path != ":memory:") {
        auto dir = config_.path.substr(0, config_.path.rfind('/'));
        if (!dir.empty()) {
            auto r = fs::mkdir_p(dir);
            if (!r.ok()) return r;
        }
    }

    int rc = sqlite3_open(config_.path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return Error::db("sqlite3_open failed: " + msg);
    }

    // WAL mode for concurrent access
    if (config_.enable_wal && config_.path != ":memory:") {
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    }

    // Foreign keys for CASCADE delete
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    opened_ = true;
    return create_tables();
}

Result<void> SqliteConversationStore::close() {
    if (!opened_) return {};
    sqlite3_close(db_);
    db_ = nullptr;
    opened_ = false;
    return {};
}

Result<void> SqliteConversationStore::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id          TEXT PRIMARY KEY,
            title       TEXT NOT NULL DEFAULT '',
            model       TEXT DEFAULT '',
            created_at  TEXT NOT NULL,
            updated_at  TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS messages (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id TEXT NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
            seq             INTEGER NOT NULL,
            role            TEXT NOT NULL,
            content         TEXT NOT NULL DEFAULT '',
            extra_json      TEXT DEFAULT '{}'
        );
        CREATE INDEX IF NOT EXISTS idx_messages_conv_seq ON messages(conversation_id, seq);
        CREATE INDEX IF NOT EXISTS idx_conversations_updated ON conversations(updated_at DESC);
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        return Error::db("create tables failed: " + msg);
    }
    return {};
}

// --- Role conversion ---

std::string SqliteConversationStore::role_to_string(Role role) const {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "unknown";
}

Role SqliteConversationStore::string_to_role(const std::string& s) const {
    if (s == "system")    return Role::System;
    if (s == "user")      return Role::User;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool")      return Role::Tool;
    return Role::System;  // fallback
}

// --- Message serialization ---

json SqliteConversationStore::message_to_extra_json(const Message& msg) const {
    json extra;
    if (msg.name.has_value()) {
        extra["name"] = msg.name.value();
    }
    if (msg.tool_calls.has_value()) {
        json tc_arr = json::array();
        for (const auto& tc : msg.tool_calls.value()) {
            json tc_obj;
            tc_obj["id"] = tc.id;
            tc_obj["name"] = tc.name;
            tc_obj["arguments"] = tc.arguments;
            tc_arr.push_back(tc_obj);
        }
        extra["tool_calls"] = tc_arr;
    }
    if (msg.tool_call_id.has_value()) {
        extra["tool_call_id"] = msg.tool_call_id.value();
    }
    return extra;
}

Message SqliteConversationStore::row_to_message(const std::string& role,
                                                  const std::string& content,
                                                  const std::string& extra_json_str) const {
    Message msg;
    msg.role = string_to_role(role);
    msg.content = content;

    if (!extra_json_str.empty() && extra_json_str != "{}") {
        try {
            auto extra = json::parse(extra_json_str);
            if (extra.contains("name")) {
                msg.name = extra["name"].get<std::string>();
            }
            if (extra.contains("tool_calls")) {
                std::vector<ToolCall> tcs;
                for (const auto& tc_json : extra["tool_calls"]) {
                    ToolCall tc;
                    tc.id = tc_json["id"].get<std::string>();
                    tc.name = tc_json["name"].get<std::string>();
                    tc.arguments = tc_json["arguments"];
                    tcs.push_back(std::move(tc));
                }
                msg.tool_calls = std::move(tcs);
            }
            if (extra.contains("tool_call_id")) {
                msg.tool_call_id = extra["tool_call_id"].get<std::string>();
            }
        } catch (...) {
            // Malformed extra_json — skip optional fields
        }
    }
    return msg;
}

// --- CRUD ---

Result<std::string> SqliteConversationStore::create(const std::string& model) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string id = generate_conv_id();
    std::string now = current_iso8601();

    const char* sql = "INSERT INTO conversations (id, title, model, created_at, updated_at) VALUES (?, '', ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    return id;
}

Result<void> SqliteConversationStore::append(const std::string& conversation_id,
                                               const Message& msg) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Get next seq number
    const char* seq_sql = "SELECT COALESCE(MAX(seq), -1) + 1 FROM messages WHERE conversation_id = ?";
    sqlite3_stmt* seq_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, seq_sql, -1, &seq_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare seq failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(seq_stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    int next_seq = 0;
    rc = sqlite3_step(seq_stmt);
    if (rc == SQLITE_ROW) {
        next_seq = sqlite3_column_int(seq_stmt, 0);
    }
    sqlite3_finalize(seq_stmt);

    // Insert message
    std::string extra = message_to_extra_json(msg).dump();
    std::string role_str = role_to_string(msg.role);

    const char* sql = "INSERT INTO messages (conversation_id, seq, role, content, extra_json) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, next_seq);
    sqlite3_bind_text(stmt, 3, role_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, extra.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert message failed: ") + sqlite3_errmsg(db_));
    }

    // Update conversation metadata (title, updated_at)
    std::string first_user_content;
    if (msg.role == Role::User) {
        first_user_content = msg.content;
    }
    return update_meta_on_append(conversation_id, first_user_content);
}

Result<void> SqliteConversationStore::update_meta_on_append(
        const std::string& conversation_id,
        const std::string& first_user_content) {
    std::string now = current_iso8601();

    // Build UPDATE statement — optionally set title
    std::string sql;
    if (!first_user_content.empty()) {
        // Check if title is still empty
        sql = "UPDATE conversations SET updated_at = ?, "
              "title = CASE WHEN title = '' THEN ? ELSE title END "
              "WHERE id = ?";
    } else {
        sql = "UPDATE conversations SET updated_at = ? WHERE id = ?";
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare update failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
    if (!first_user_content.empty()) {
        std::string truncated = first_user_content.substr(0, 50);
        sqlite3_bind_text(stmt, 2, truncated.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_text(stmt, 2, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("update conversation failed: ") + sqlite3_errmsg(db_));
    }
    return {};
}

Result<std::vector<Message>> SqliteConversationStore::load(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Verify conversation exists
    const char* check_sql = "SELECT id FROM conversations WHERE id = ?";
    sqlite3_stmt* check_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare check failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(check_stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(check_stmt);
    sqlite3_finalize(check_stmt);

    if (rc != SQLITE_ROW) {
        return Error::not_found("Conversation not found: " + conversation_id);
    }

    // Load messages
    const char* sql = "SELECT role, content, extra_json FROM messages "
                      "WHERE conversation_id = ? ORDER BY seq ASC";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<Message> messages;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::string role(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        std::string content(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        std::string extra(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        messages.push_back(row_to_message(role, content, extra));
    }
    sqlite3_finalize(stmt);
    return messages;
}

Result<std::vector<ConversationMeta>> SqliteConversationStore::list(int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = R"(
        SELECT c.id, c.title, c.model, c.created_at, c.updated_at,
               COUNT(m.id) as msg_count
        FROM conversations c
        LEFT JOIN messages m ON c.id = m.conversation_id
        GROUP BY c.id
        ORDER BY c.updated_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    std::vector<ConversationMeta> result;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ConversationMeta meta;
        meta.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        meta.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        meta.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        meta.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        meta.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        meta.message_count = sqlite3_column_int(stmt, 5);
        result.push_back(std::move(meta));
    }
    sqlite3_finalize(stmt);
    return result;
}

Result<ConversationMeta> SqliteConversationStore::get_meta(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = R"(
        SELECT c.id, c.title, c.model, c.created_at, c.updated_at,
               COUNT(m.id) as msg_count
        FROM conversations c
        LEFT JOIN messages m ON c.id = m.conversation_id
        WHERE c.id = ?
        GROUP BY c.id
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return Error::not_found("Conversation not found: " + conversation_id);
    }

    ConversationMeta meta;
    meta.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    meta.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    meta.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    meta.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    meta.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    meta.message_count = sqlite3_column_int(stmt, 5);
    sqlite3_finalize(stmt);
    return meta;
}

Result<bool> SqliteConversationStore::remove(const std::string& conversation_id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "DELETE FROM conversations WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }

    return sqlite3_changes(db_) > 0;
}

// --- JSONL ---

Result<std::string> SqliteConversationStore::export_jsonl(const std::string& conversation_id) {
    auto msgs = load(conversation_id);
    if (!msgs.ok()) return msgs.error();

    std::string result;
    for (const auto& msg : msgs.value()) {
        json line;
        line["role"] = role_to_string(msg.role);
        line["content"] = msg.content;
        if (msg.name.has_value()) {
            line["name"] = msg.name.value();
        }
        if (msg.tool_calls.has_value()) {
            json tc_arr = json::array();
            for (const auto& tc : msg.tool_calls.value()) {
                json tc_obj;
                tc_obj["id"] = tc.id;
                tc_obj["name"] = tc.name;
                tc_obj["arguments"] = tc.arguments;
                tc_arr.push_back(tc_obj);
            }
            line["tool_calls"] = tc_arr;
        }
        if (msg.tool_call_id.has_value()) {
            line["tool_call_id"] = msg.tool_call_id.value();
        }
        result += line.dump() + "\n";
    }
    return result;
}

Result<std::string> SqliteConversationStore::import_jsonl(const std::string& jsonl_data,
                                                            const std::string& model) {
    auto r = open();
    if (!r.ok()) return r.error();

    // Create new conversation
    auto conv_r = create(model);
    if (!conv_r.ok()) return conv_r.error();
    std::string conv_id = conv_r.value();

    // Parse each line
    std::istringstream stream(jsonl_data);
    std::string line;
    int line_num = 0;
    while (std::getline(stream, line)) {
        line_num++;
        if (line.empty()) continue;

        json obj;
        try {
            obj = json::parse(line);
        } catch (...) {
            return Error::parse("JSONL parse error at line " + std::to_string(line_num));
        }

        if (!obj.contains("role") || !obj["role"].is_string()) {
            return Error::parse("JSONL missing 'role' at line " + std::to_string(line_num));
        }

        Message msg;
        msg.role = string_to_role(obj["role"].get<std::string>());
        msg.content = obj.value("content", std::string{});

        if (obj.contains("name") && obj["name"].is_string()) {
            msg.name = obj["name"].get<std::string>();
        }
        if (obj.contains("tool_calls") && obj["tool_calls"].is_array()) {
            std::vector<ToolCall> tcs;
            for (const auto& tc_json : obj["tool_calls"]) {
                ToolCall tc;
                tc.id = tc_json.value("id", std::string{});
                tc.name = tc_json.value("name", std::string{});
                if (tc_json.contains("arguments")) {
                    tc.arguments = tc_json["arguments"];
                }
                tcs.push_back(std::move(tc));
            }
            msg.tool_calls = std::move(tcs);
        }
        if (obj.contains("tool_call_id") && obj["tool_call_id"].is_string()) {
            msg.tool_call_id = obj["tool_call_id"].get<std::string>();
        }

        auto append_r = append(conv_id, msg);
        if (!append_r.ok()) {
            // Best effort: remove partial conversation
            remove(conv_id);
            return append_r.error();
        }
    }

    return conv_id;
}

}  // namespace ea::conversation
```

- [ ] **Step 5: Build and run tests**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[conversation]" 2>&1
```

Expected: All 16 conversation tests pass.

- [ ] **Step 6: Run full test suite to verify no regressions**

```bash
cd build && ./tests/ea-tests 2>&1 | tail -5
```

Expected: All tests pass (previous count + 16 new).

- [ ] **Step 7: Commit**

```bash
git add src/conversation/SqliteConversationStore.cpp tests/test_conversation_store.cpp tests/CMakeLists.txt
git commit -m "feat: implement SqliteConversationStore with CRUD and JSONL export/import"
```

---

### Task 3: AgentLoop Integration

**Files:**
- Modify: `src/agent/AgentLoop.h` (add `conv_store_`, `conversation_id_`, `saved_count_`, `auto_persist`, `restore_conversation()`, constructor param)
- Modify: `src/agent/AgentLoop.cpp` (auto-persist in `run()`, `restore_conversation()`, `clear_history()` update)
- Modify: `src/agent/CMakeLists.txt` (add `ea-conversation` dependency)

**Interfaces:**
- Consumes: `ea::conversation::IConversationStore` from Task 1
- Produces: `AgentLoop::restore_conversation()`, auto-persist behavior in `run()`

- [ ] **Step 1: Write the failing integration test**

Create `tests/test_conversation_integration.cpp`:

```cpp
// tests/test_conversation_integration.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/AgentLoop.h"
#include "conversation/SqliteConversationStore.h"
#include "memory/InMemoryBackend.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "core/Types.h"
#include <cstdio>

using namespace ea;
using namespace ea::agent;
using namespace ea::conversation;
using namespace ea::memory;

// Minimal mock provider that echoes user input
class EchoProvider : public ea::IProvider {
public:
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
                              const ChatOptions&,
                              std::function<void(const StreamChunk&)>) override {
        return Error::not_found("not implemented");
    }
    std::vector<std::string> list_models() override { return {"echo"}; }
    ProviderCapabilities capabilities() const override { return {false, false}; }
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
        nullptr, nullptr, nullptr, nullptr, &f.conv_store
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
        nullptr, nullptr, nullptr, nullptr, &f.conv_store
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
        nullptr, nullptr, nullptr, nullptr, nullptr  // no conv_store
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
        nullptr, nullptr, nullptr, nullptr, &f.conv_store
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
```

- [ ] **Step 2: Add integration test to tests/CMakeLists.txt**

Add `test_conversation_integration.cpp` to the `ea-tests` source list (after `test_conversation_store.cpp`):

```cmake
add_executable(ea-tests
    ...
    test_conversation_store.cpp
    test_conversation_integration.cpp
)
```

- [ ] **Step 3: Run tests to verify they fail**

```bash
cd build && cmake --build . -j$(nproc) 2>&1 | tail -20
```

Expected: Build fails because `AgentLoop` doesn't have `conv_store_`, `conversation_id_`, `restore_conversation()`, or the new constructor parameter yet.

- [ ] **Step 4: Modify AgentLoop.h**

Add `#include "conversation/IConversationStore.h"` at the top.

Add to `Config` struct:
```cpp
bool auto_persist = true;
```

Add constructor parameter (after `IMemoryStrategy* strategy`):
```cpp
IConversationStore* conv_store = nullptr
```

Add new public method:
```cpp
void restore_conversation(const std::string& conversation_id,
                          std::vector<Message> messages);
const std::string& conversation_id() const { return conversation_id_; }
```

Add new private members:
```cpp
IConversationStore* conv_store_;
std::string conversation_id_;
size_t saved_count_ = 0;
```

Full modified `AgentLoop.h`:
```cpp
// AgentLoop — orchestrates ITurnStep chain for agent execution
#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "IEventListener.h"
#include "LoopDetector.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "IMemoryStrategy.h"
#include "conversation/IConversationStore.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

namespace ea::agent {

using ToolRegistry = tool::ToolRegistry;

class AgentLoop {
public:
    struct Config {
        int max_iterations = 90;
        int max_tool_output_bytes = 65536;
        int max_messages = 100;
        bool auto_memory = true;
        bool stream = true;
        bool auto_persist = true;
    };

    using OutputFn = std::function<void(const std::string&)>;
    using StreamFn = std::function<void(const StreamChunk&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output,
              StreamFn stream_fn = nullptr,
              security::SecurityPolicy* policy = nullptr,
              security::IApprovalHandler* approval = nullptr,
              ContextCompressor* compressor = nullptr,
              IMemoryStrategy* strategy = nullptr,
              conversation::IConversationStore* conv_store = nullptr);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

    // Conversation persistence
    void restore_conversation(const std::string& conversation_id,
                              std::vector<Message> messages);
    const std::string& conversation_id() const { return conversation_id_; }

    // Step chain customization
    void add_step(std::unique_ptr<ITurnStep> step);
    void set_steps(std::vector<std::unique_ptr<ITurnStep>> steps);

    // Event listener management
    void add_listener(std::shared_ptr<IEventListener> listener);
    void remove_listener(const std::shared_ptr<IEventListener>& listener);

private:
    void build_system_prompt_once();
    void emit(AgentEventType type, const TurnContext& ctx);
    void emit_event(const AgentEvent& event);
    void persist_new_messages();

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;
    StreamFn stream_fn_;
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;
    ContextCompressor* compressor_;
    IMemoryStrategy* strategy_;
    conversation::IConversationStore* conv_store_;

    std::vector<Message> history_;
    std::string base_system_prompt_;
    std::string system_prompt_;
    std::string conversation_id_;
    size_t saved_count_ = 0;
    std::atomic<bool> interrupted_{false};

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
    std::vector<std::shared_ptr<IEventListener>> listeners_;
};

}  // namespace ea::agent
```

- [ ] **Step 5: Modify AgentLoop.cpp**

Update constructor to add `conv_store_(conv_store)` initializer:

```cpp
AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output,
                     StreamFn stream_fn,
                     security::SecurityPolicy* policy,
                     security::IApprovalHandler* approval,
                     ContextCompressor* compressor,
                     IMemoryStrategy* strategy,
                     conversation::IConversationStore* conv_store)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
    , stream_fn_(std::move(stream_fn))
    , policy_(policy)
    , approval_(approval)
    , compressor_(compressor)
    , strategy_(strategy)
    , conv_store_(conv_store)
{
    // ... existing step chain setup ...
}
```

Add `persist_new_messages()` helper method:

```cpp
void AgentLoop::persist_new_messages() {
    if (!conv_store_ || !config_.auto_persist || conversation_id_.empty()) return;

    for (size_t i = saved_count_; i < history_.size(); ++i) {
        auto r = conv_store_->append(conversation_id_, history_[i]);
        if (!r.ok()) {
            EA_WARN("Failed to persist message: {}", r.error().message);
            // Don't block — just log and continue
        }
    }
    saved_count_ = history_.size();
}
```

In `run()`, add auto-persist logic:

After `build_system_prompt_once()` and strategy prompt refresh, add conversation creation:

```cpp
// Create conversation on first run
if (conv_store_ && config_.auto_persist && conversation_id_.empty()) {
    auto r = conv_store_->create();
    if (r.ok()) {
        conversation_id_ = r.value();
        saved_count_ = 0;
    } else {
        EA_WARN("Failed to create conversation: {}", r.error().message);
    }
}
```

In the `should_stop` branch (after `strategy_->on_turn_end()`), add:

```cpp
persist_new_messages();
```

In the `max_iterations` fallback (after `strategy_->on_turn_end()`), add:

```cpp
persist_new_messages();
```

Add `restore_conversation()` implementation:

```cpp
void AgentLoop::restore_conversation(const std::string& conversation_id,
                                      std::vector<Message> messages) {
    conversation_id_ = conversation_id;
    history_ = std::move(messages);
    saved_count_ = history_.size();
    base_system_prompt_.clear();  // Force rebuild on next run()
}
```

Update `clear_history()`:

```cpp
void AgentLoop::clear_history() {
    history_.clear();
    base_system_prompt_.clear();
    system_prompt_.clear();
    conversation_id_.clear();
    saved_count_ = 0;
}
```

- [ ] **Step 6: Modify src/agent/CMakeLists.txt**

Add `ea-conversation` to the link libraries:

```cmake
target_link_libraries(ea-agent PUBLIC ea-core ea-common ea-platform ea-tool ea-conversation)
```

- [ ] **Step 7: Build and run tests**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[agent][conversation]" 2>&1
```

Expected: All 4 integration tests pass.

- [ ] **Step 8: Run full test suite**

```bash
cd build && ./tests/ea-tests 2>&1 | tail -5
```

Expected: All tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/agent/AgentLoop.h src/agent/AgentLoop.cpp src/agent/CMakeLists.txt tests/test_conversation_integration.cpp tests/CMakeLists.txt
git commit -m "feat: integrate IConversationStore into AgentLoop with auto-persist and restore"
```

---

### Task 4: Configuration Support

**Files:**
- Modify: `src/config/Config.h` (add `ConversationConfig` struct and field in `AppConfig`)
- Modify: `src/config/Config.cpp` (add `[conversation]` TOML parsing)

**Interfaces:**
- Consumes: `toml11` for TOML parsing
- Produces: `ea::config::ConversationConfig` with `path`, `auto_resume`, `auto_persist`, `max_conversations`

- [ ] **Step 1: Add ConversationConfig to Config.h**

Add after `MemoryStrategyConfig`:

```cpp
struct ConversationConfig {
    std::string path;             // conversations.db path (empty = auto)
    bool auto_resume = true;      // CLI: auto-resume last conversation
    bool auto_persist = true;     // Auto-save messages each turn
    int max_conversations = 1000; // Max stored conversations
};
```

Add to `AppConfig`:

```cpp
struct AppConfig {
    ProviderConfig provider;
    MemoryConfig memory;
    MemoryStrategyConfig memory_strategy;
    ConversationConfig conversation;   // NEW
    SecurityConfig security;
    AgentConfig agent;
    std::vector<McpServerConfig> mcp_servers;
    ServerConfig server;
    std::string config_path;
};
```

- [ ] **Step 2: Add TOML parsing to Config.cpp**

Add after the `[server]` parsing block (before the closing `catch`):

```cpp
if (data.contains("conversation")) {
    auto conv = toml::find(data, "conversation");
    cfg.conversation.path = toml::find_or<std::string>(conv, "path", cfg.conversation.path);
    cfg.conversation.auto_resume = toml::find_or<bool>(conv, "auto_resume", cfg.conversation.auto_resume);
    cfg.conversation.auto_persist = toml::find_or<bool>(conv, "auto_persist", cfg.conversation.auto_persist);
    cfg.conversation.max_conversations = toml::find_or<int>(conv, "max_conversations", cfg.conversation.max_conversations);
}
```

- [ ] **Step 3: Build to verify: Build**

```bash
cd build && cmake --build . -j$(nproc) 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp
git commit -m "feat: add ConversationConfig with TOML parsing"
```

---

### Task 5: main.cpp Wiring + CLI Commands

**Files:**
- Modify: `src/main.cpp` (create ConversationStore, auto-resume, CLI commands)

**Interfaces:**
- Consumes: `ConversationConfig` from Task 4, `SqliteConversationStore` from Task 2, `AgentLoop` from Task 3
- Produces: CLI `/history`, `/resume`, `/export`, `/import` commands; auto-resume on startup

- [ ] **Step 1: Add includes to main.cpp**

Add after the existing includes:

```cpp
#include "conversation/SqliteConversationStore.h"
```

- [ ] **Step 2: Create ConversationStore (section 5.8)**

Add after section 5.7 (memory strategy creation), before section 6 (HTTP client):

```cpp
// 5.8. Create conversation store
std::unique_ptr<ea::conversation::SqliteConversationStore> conv_store;
std::string conv_path = cfg.conversation.path;
if (conv_path.empty()) {
    auto data_dir = ea::fs::config_dir();
    if (data_dir.ok()) {
        conv_path = data_dir.value() + "/conversations.db";
    } else {
        conv_path = home + "/.embedded-agent/conversations.db";
    }
}
conv_store = std::make_unique<ea::conversation::SqliteConversationStore>(
    ea::conversation::SqliteConversationStore::Config{conv_path});
auto conv_open = conv_store->open();
if (!conv_open.ok()) {
    EA_ERROR("Conversation store open failed: {}", conv_open.error().message);
    conv_store.reset();  // Continue without persistence
}
```

- [ ] **Step 3: Pass conv_store to AgentLoop constructor**

Update the AgentLoop construction (section 8) to pass `conv_store.get()`:

```cpp
ea::agent::AgentLoop loop(
    provider.get(), &registry, memory.get(),
    ea::agent::AgentLoop::Config{
        cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream, cfg.conversation.auto_persist
    },
    [](const std::string& text) { std::cout << text << std::endl; },
    stream_fn,
    security.get(),
    approval.get(),
    compressor.get(),
    strategy.get(),
    conv_store.get()   // NEW
);
```

Note: `AgentLoop::Config` is an aggregate struct, so we initialize it with positional values. The new `auto_persist` field is added after `stream`.

- [ ] **Step 4: Add auto-resume in CLI mode**

In the `#else` (CLI mode) section, after creating the AgentLoop and before the interactive loop, add:

```cpp
// Auto-resume last conversation
if (conv_store && cfg.conversation.auto_resume) {
    auto recent = conv_store->list(1, 0);
    if (recent.ok() && !recent.value().empty()) {
        auto& meta = recent.value()[0];
        auto msgs = conv_store->load(meta.id);
        if (msgs.ok() && !msgs.value().empty()) {
            loop.restore_conversation(meta.id, std::move(msgs.value()));
            std::cout << "Resumed: " << meta.title
                      << " (" << meta.message_count << " messages)" << std::endl;
        }
    }
}
```

- [ ] **Step 5: Add CLI commands in the interactive loop**

In the CLI `while (true)` loop, after the `/quit` and empty input checks, add:

```cpp
if (input == "/history") {
    if (conv_store) {
        auto list = conv_store->list(10, 0);
        if (list.ok()) {
            for (const auto& m : list.value()) {
                std::cout << "  " << m.id << "  " << m.title
                          << "  (" << m.message_count << " msgs, " << m.updated_at << ")" << std::endl;
            }
        }
    } else {
        std::cout << "Conversation persistence not available" << std::endl;
    }
    continue;
}
if (input.substr(0, 8) == "/resume ") {
    if (conv_store) {
        std::string cid = input.substr(8);
        auto msgs = conv_store->load(cid);
        if (msgs.ok()) {
            loop.restore_conversation(cid, std::move(msgs.value()));
            auto meta = conv_store->get_meta(cid);
            std::cout << "Resumed: " << (meta.ok() ? meta.value().title : cid) << std::endl;
        } else {
            std::cout << "Conversation not found: " << cid << std::endl;
        }
    }
    continue;
}
if (input == "/export") {
    if (conv_store && !loop.conversation_id().empty()) {
        auto data = conv_store->export_jsonl(loop.conversation_id());
        if (data.ok()) {
            std::cout << data.value();
        } else {
            std::cout << "Export failed: " << data.error().message << std::endl;
        }
    } else {
        std::cout << "No active conversation to export" << std::endl;
    }
    continue;
}
if (input.substr(0, 8) == "/import ") {
    if (conv_store) {
        std::string filepath = input.substr(8);
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "Cannot open file: " << filepath << std::endl;
            continue;
        }
        std::string jsonl_data((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        auto new_id = conv_store->import_jsonl(jsonl_data);
        if (new_id.ok()) {
            std::cout << "Imported conversation: " << new_id.value() << std::endl;
        } else {
            std::cout << "Import failed: " << new_id.error().message << std::endl;
        }
    }
    continue;
}
```

Also add `#include <fstream>` at the top of main.cpp.

- [ ] **Step 6: Build**

```bash
cd build && cmake --build . -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire ConversationStore in main.cpp with auto-resume and CLI commands"
```

---

### Task 6: Server Mode Integration

**Files:**
- Modify: `src/server/SessionManager.h` (add `conv_store_` member, `conversation_id` param)
- Modify: `src/server/SessionManager.cpp` (pass `conv_store_` to AgentLoop, handle `conversation_id`)
- Modify: `src/server/HttpServer.h` (add `conv_store_` member, constructor param)
- Modify: `src/server/HttpServer.cpp` (add `/api/conversations/*` endpoints, pass `conv_store_` to SessionManager, extend session creation)
- Modify: `src/server/CMakeLists.txt` (add `ea-conversation` dependency)

**Interfaces:**
- Consumes: `IConversationStore` from Task 1, `SqliteConversationStore` from Task 2
- Produces: `/api/conversations/*` HTTP endpoints, session creation with `conversation_id`

- [ ] **Step 1: Modify SessionManager.h**

Add include:
```cpp
#include "conversation/IConversationStore.h"
```

Add `conv_store_` member and `conversation_id` parameter to `create()`:

```cpp
class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config,
                            conversation::IConversationStore* conv_store = nullptr);

    Session* create(IProvider* provider,
                    tool::ToolRegistry* registry,
                    IMemory* shared_backend,
                    security::SecurityPolicy* policy,
                    const AgentLoop::Config& loop_cfg,
                    const std::string& model = "",
                    const std::string& system_prompt = "",
                    const std::string& conversation_id = "");

    Session* get(const std::string& id);
    bool remove(const std::string& id);
    std::vector<Session*> list();
    void cleanup_idle();

private:
    std::string generate_id();

    ServerConfig config_;
    conversation::IConversationStore* conv_store_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::mt19937 rng_{std::random_device{}()};
};
```

- [ ] **Step 2: Modify SessionManager.cpp**

Update constructor:
```cpp
SessionManager::SessionManager(const ServerConfig& config,
                                conversation::IConversationStore* conv_store)
    : config_(config), conv_store_(conv_store) {}
```

Update `create()` to accept and use `conversation_id`:

```cpp
Session* SessionManager::create(IProvider* provider,
                                 tool::ToolRegistry* registry,
                                 IMemory* shared_backend,
                                 security::SecurityPolicy* policy,
                                 const AgentLoop::Config& loop_cfg,
                                 const std::string& model,
                                 const std::string& /*system_prompt*/,
                                 const std::string& conversation_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (static_cast<int>(sessions_.size()) >= config_.max_sessions) {
        EA_WARN("Max sessions ({}) reached, cannot create new session", config_.max_sessions);
        return nullptr;
    }

    auto session = std::make_unique<Session>();
    session->id = generate_id();
    session->model = model;
    session->last_active = std::chrono::steady_clock::now();

    // Create scoped memory for this session
    memory::MemoryScope scope;
    scope.agent_id = session->id;
    auto per_session_backend = std::make_unique<memory::InMemoryBackend>();
    session->memory = std::make_unique<memory::ScopedMemory>(
        std::move(per_session_backend), scope);

    // Create per-session approval handler
    session->approval = std::make_unique<security::PendingApprovalHandler>(300);

    // Create AgentLoop with conv_store
    session->loop = std::make_shared<AgentLoop>(
        provider,
        registry,
        session->memory.get(),
        loop_cfg,
        [](const std::string& text) { (void)text; },
        nullptr,
        policy,
        session->approval.get(),
        nullptr,   // compressor
        nullptr,   // strategy (TODO from Phase 4D)
        conv_store_
    );

    // Restore conversation if conversation_id provided
    if (!conversation_id.empty() && conv_store_) {
        auto msgs = conv_store_->load(conversation_id);
        if (msgs.ok() && !msgs.value().empty()) {
            session->loop->restore_conversation(conversation_id, std::move(msgs.value()));
            EA_INFO("Session {} restored conversation {}", session->id, conversation_id);
        } else {
            EA_WARN("Failed to restore conversation {}: {}",
                    conversation_id, msgs.ok() ? "empty" : msgs.error().message);
        }
    }

    auto* ptr = session.get();
    sessions_[session->id] = std::move(session);

    EA_INFO("Session created: {}", ptr->id);
    return ptr;
}
```

- [ ] **Step 3: Modify HttpServer.h**

Add include and constructor parameter:

```cpp
#include "conversation/IConversationStore.h"

class HttpServer {
public:
    HttpServer(ServerConfig config,
               IProvider* provider,
               tool::ToolRegistry* registry,
               security::SecurityPolicy* policy,
               IMemory* shared_memory = nullptr,
               conversation::IConversationStore* conv_store = nullptr);
    // ... rest unchanged ...

private:
    // ... existing members ...
    conversation::IConversationStore* conv_store_;
};
```

- [ ] **Step 4: Modify HttpServer.cpp**

Update constructor:
```cpp
HttpServer::HttpServer(ServerConfig config,
                       IProvider* provider,
                       tool::ToolRegistry* registry,
                       security::SecurityPolicy* policy,
                       IMemory* shared_memory,
                       conversation::IConversationStore* conv_store)
    : config_(std::move(config))
    , server_(std::make_unique<httplib::Server>())
    , sessions_(config_, conv_store)
    , provider_(provider)
    , registry_(registry)
    , policy_(policy)
    , shared_memory_(shared_memory)
    , conv_store_(conv_store)
    , start_time_(std::chrono::steady_clock::now()) {
    setup_routes();
}
```

Update session creation in `POST /api/sessions` to pass `conversation_id`:

```cpp
std::string conversation_id = body.value("conversation_id", "");
auto* session = sessions_.create(provider_, registry_, shared_memory_, policy_, loop_cfg, model, system_prompt, conversation_id);
```

Add `conversation_id` to session creation response:
```cpp
resp["id"] = session->id;
resp["conversation_id"] = session->loop->conversation_id();
resp["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
```

Add conversation API endpoints in `setup_routes()` (after the approval endpoints):

```cpp
// --- Conversation API ---
if (conv_store_) {
    // List conversations
    server_->Get("/api/conversations", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = std::stoi(req.get_param_value("limit").empty() ? "50" : req.get_param_value("limit"));
        int offset = std::stoi(req.get_param_value("offset").empty() ? "0" : req.get_param_value("offset"));

        auto list = conv_store_->list(limit, offset);
        json arr = json::array();
        if (list.ok()) {
            for (const auto& m : list.value()) {
                json obj;
                obj["id"] = m.id;
                obj["title"] = m.title;
                obj["model"] = m.model;
                obj["created_at"] = m.created_at;
                obj["updated_at"] = m.updated_at;
                obj["message_count"] = m.message_count;
                arr.push_back(obj);
            }
        }
        set_cors_headers(&res);
        res.set_content(json{{"conversations", arr}}.dump(), "application/json");
    });

    // Get conversation metadata
    server_->Get(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto meta = conv_store_->get_meta(id);
        if (!meta.ok()) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
            return;
        }
        json obj;
        obj["id"] = meta.value().id;
        obj["title"] = meta.value().title;
        obj["model"] = meta.value().model;
        obj["created_at"] = meta.value().created_at;
        obj["updated_at"] = meta.value().updated_at;
        obj["message_count"] = meta.value().message_count;
        set_cors_headers(&res);
        res.set_content(obj.dump(), "application/json");
    });

    // Get conversation messages
    server_->Get(R"(/api/conversations/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto msgs = conv_store_->load(id);
        if (!msgs.ok()) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
            return;
        }
        json messages = json::array();
        for (const auto& msg : msgs.value()) {
            json m;
            switch (msg.role) {
                case Role::System:    m["role"] = "system"; break;
                case Role::User:      m["role"] = "user"; break;
                case Role::Assistant: m["role"] = "assistant"; break;
                case Role::Tool:      m["role"] = "tool"; break;
            }
            m["content"] = msg.content;
            if (msg.name.has_value()) m["name"] = msg.name.value();
            if (msg.tool_calls.has_value()) {
                json tc_arr = json::array();
                for (const auto& tc : msg.tool_calls.value()) {
                    json tc_obj;
                    tc_obj["id"] = tc.id;
                    tc_obj["name"] = tc.name;
                    tc_obj["arguments"] = tc.arguments;
                    tc_arr.push_back(tc_obj);
                }
                m["tool_calls"] = tc_arr;
            }
            if (msg.tool_call_id.has_value()) m["tool_call_id"] = msg.tool_call_id.value();
            messages.push_back(m);
        }
        set_cors_headers(&res);
        res.set_content(json{{"messages", messages}}.dump(), "application/json");
    });

    // Delete conversation
    server_->Delete(R"(/api/conversations/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto r = conv_store_->remove(id);
        if (!r.ok()) {
            set_cors_headers(&res);
            res.status = 500;
            res.set_content(error_response("delete_failed", r.error().message).dump(), "application/json");
            return;
        }
        set_cors_headers(&res);
        res.set_content(json{{"ok", true}, {"deleted", r.value()}}.dump(), "application/json");
    });

    // Export conversation as JSONL
    server_->Get(R"(/api/conversations/([^/]+)/export)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        auto data = conv_store_->export_jsonl(id);
        if (!data.ok()) {
            set_cors_headers(&res);
            res.status = 404;
            res.set_content(error_response("not_found", "Conversation " + id + " not found").dump(), "application/json");
            return;
        }
        set_cors_headers(&res);
        res.set_content(data.value(), "application/x-jsonl");
    });

    // Import conversation from JSONL
    server_->Post("/api/conversations/import", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
            return;
        }
        if (!body.contains("data") || !body["data"].is_string()) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("missing_field", "Request must contain a 'data' string field").dump(), "application/json");
            return;
        }
        std::string jsonl_data = body["data"].get<std::string>();
        std::string model = body.value("model", "");
        auto new_id = conv_store_->import_jsonl(jsonl_data, model);
        if (!new_id.ok()) {
            set_cors_headers(&res);
            res.status = 400;
            res.set_content(error_response("import_failed", new_id.error().message).dump(), "application/json");
            return;
        }
        set_cors_headers(&res);
        res.set_content(json{{"id", new_id.value()}}.dump(), "application/json");
    });
}
```

- [ ] **Step 5: Modify src/server/CMakeLists.txt**

Add `ea-conversation` to link libraries:

```cmake
target_link_libraries(ea-server PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json ea-conversation PRIVATE httplib::httplib)
```

- [ ] **Step 6: Update main.cpp HttpServer construction**

In the Server mode section, pass `conv_store.get()`:

```cpp
auto http_server = std::make_unique<ea::server::HttpServer>(
    srv_cfg, provider.get(), &registry, security.get(), memory.get(), conv_store.get()
);
```

- [ ] **Step 7: Build and run full test suite**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-tests 2>&1 | tail -5
```

Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/server/ src/main.cpp
git commit -m "feat: add Server conversation API endpoints and session restoration"
```

---

### Task 7: Final Verification

**Files:** None (verification only)

- [ ] **Step 1: Clean rebuild**

```bash
cd build && cmake --build . --clean-first -j$(nproc) 2>&1 | tail -10
```

Expected: Build succeeds with 0 warnings.

- [ ] **Step 2: Run full test suite**

```bash
cd build && ./tests/ea-tests 2>&1 | tail -10
```

Expected: All tests pass. Count should be previous total + 16 (conversation store) + 4 (integration) = 390+.

- [ ] **Step 3: Run conversation-specific tests**

```bash
cd build && ./tests/ea-tests "[conversation]" 2>&1
```

Expected: All 20 conversation tests pass (16 unit + 4 integration).

- [ ] **Step 4: Verify no regressions in existing test tags**

```bash
cd build && ./tests/ea-tests "[memory]" "[agent]" "[server]" 2>&1 | tail -10
```

Expected: All pass.

- [ ] **Step 5: Commit (if any fixes needed)**

```bash
git add -A && git commit -m "fix: address final verification findings"
```

If no fixes needed, skip this step.
