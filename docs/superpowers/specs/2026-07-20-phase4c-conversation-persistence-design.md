# Phase 4C: Conversation Persistence — Design Spec

**Date**: 2026-07-20
**Status**: Draft
**Depends on**: Phase 4A (OllamaProvider), Phase 4B (Server Mode)

## Problem

Conversation history lives only in memory (`AgentLoop::history_`). When the process exits (CLI) or a session expires (Server), all context is lost. Users cannot:

- Resume a previous conversation after restarting
- Export or import conversations
- Review past conversations in Server mode

The existing `SqliteMemory` stores *facts* (key-value memory entries), not *conversations* (ordered message sequences). These are fundamentally different data models: conversations require ordered, atomic multi-message storage with metadata; memory entries are independent, searchable facts.

## Solution

Introduce an **IConversationStore** interface with a **SqliteConversationStore** implementation in a new `ea::conversation` namespace. Store conversations in a separate SQLite file (`conversations.db`) from the memory database (`memory.db`).

Key behaviors:

1. **Auto-persist**: Every `AgentLoop::run()` call automatically saves new messages to the conversation store
2. **Auto-resume**: CLI mode restores the most recent conversation on startup
3. **Manual control**: CLI commands (`/history`, `/resume`, `/export`, `/import`) and Server API endpoints for full control
4. **Graceful degradation**: Persistence failures never block the main conversation flow

## Architecture

### IConversationStore Interface

```cpp
// src/conversation/IConversationStore.h
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

### Database Schema

Stored in a separate SQLite file (`conversations.db`), independent of `memory.db`:

```sql
CREATE TABLE conversations (
    id          TEXT PRIMARY KEY,
    title       TEXT NOT NULL DEFAULT '',
    model       TEXT DEFAULT '',
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE messages (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    seq             INTEGER NOT NULL,
    role            TEXT NOT NULL,
    content         TEXT NOT NULL DEFAULT '',
    extra_json      TEXT DEFAULT '{}'
);

CREATE INDEX idx_messages_conv_seq ON messages(conversation_id, seq);
CREATE INDEX idx_conversations_updated ON conversations(updated_at DESC);
```

**extra_json** stores optional Message fields as JSON:
```json
{
    "name": "shell",
    "tool_calls": [{"id": "tc_1", "name": "shell", "arguments": {...}}],
    "tool_call_id": "tc_1"
}
```

Fields that are `std::nullopt` are omitted from the JSON object. An empty `{}` means no optional fields.

### Message Serialization

`Message` → SQLite row mapping:

| Message field | SQLite column | Notes |
|---------------|---------------|-------|
| `role` | `role` | Enum → string: "system", "user", "assistant", "tool" |
| `content` | `content` | Direct |
| `name` | `extra_json.name` | Optional |
| `tool_calls` | `extra_json.tool_calls` | Optional, JSON array |
| `tool_call_id` | `extra_json.tool_call_id` | Optional, string |

`ToolCall` serialization:
```json
{"id": "tc_1", "name": "shell", "arguments": {...}}
```

`arguments` is already `nlohmann::json`, stored as-is.

### SqliteConversationStore

```cpp
// src/conversation/SqliteConversationStore.h
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

### AgentLoop Integration

AgentLoop gains two new members and one new method:

```cpp
// AgentLoop.h additions
IConversationStore* conv_store_ = nullptr;
std::string conversation_id_;
size_t saved_count_ = 0;

// Config addition
struct Config {
    // ... existing fields ...
    bool auto_persist = true;
};

// New method
void restore_conversation(const std::string& conversation_id,
                          std::vector<Message> messages);

// Constructor addition (new parameter after strategy)
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
          IConversationStore* conv_store = nullptr);
```

**Auto-persist flow in `run()`**:

```
1. First run() with conv_store_ set:
   - conv_store_->create() → conversation_id_
   - saved_count_ = 0

2. After agent loop completes (should_stop or max_iterations):
   - For i in [saved_count_, history_.size()):
       conv_store_->append(conversation_id_, history_[i])
   - saved_count_ = history_.size()

3. Failure: log warning, do not block conversation
```

**restore_conversation()**:

```cpp
void AgentLoop::restore_conversation(const std::string& conversation_id,
                                      std::vector<Message> messages) {
    conversation_id_ = conversation_id;
    history_ = std::move(messages);
    saved_count_ = history_.size();
    base_system_prompt_.clear();  // Force rebuild on next run()
}
```

**clear_history()** update:

```cpp
void AgentLoop::clear_history() {
    history_.clear();
    base_system_prompt_.clear();
    system_prompt_.clear();
    conversation_id_.clear();  // New conversation on next run()
    saved_count_ = 0;
}
```

### CLI Mode

**Auto-resume on startup**:

```cpp
// main.cpp — after creating AgentLoop
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

**CLI commands** (added to the interactive loop in main.cpp):

| Command | Action |
|---------|--------|
| `/history` | List recent conversations |
| `/resume <id>` | Switch to a different conversation |
| `/export [id]` | Export current or specified conversation to JSONL on stdout |
| `/import <file>` | Import conversation from JSONL file |

### Server Mode

**SessionManager changes**:

```cpp
// SessionManager gains conv_store_ member
IConversationStore* conv_store_ = nullptr;

// create() gains conversation_id parameter
Session* create(IProvider* provider,
                tool::ToolRegistry* registry,
                IMemory* shared_backend,
                security::SecurityPolicy* policy,
                const AgentLoop::Config& loop_cfg,
                const std::string& model = "",
                const std::string& system_prompt = "",
                const std::string& conversation_id = "");
```

When `conversation_id` is non-empty and `conv_store_` is set:
1. `conv_store_->load(conversation_id)` → messages
2. Create AgentLoop with `conv_store_`
3. `loop->restore_conversation(conversation_id, messages)`

When `conversation_id` is empty:
- Normal session creation, but still pass `conv_store_` so messages are auto-persisted

**New HTTP API endpoints**:

```
GET    /api/conversations                    — List conversations (?limit=50&offset=0)
GET    /api/conversations/:id                — Get conversation metadata
GET    /api/conversations/:id/messages       — Get all messages
DELETE /api/conversations/:id                — Delete conversation
GET    /api/conversations/:id/export         — Export as JSONL
POST   /api/conversations/import             — Import JSONL (body: {"data": "..."})
POST   /api/sessions (extended)              — New optional "conversation_id" field
```

**Session creation with conversation_id**:

```json
// POST /api/sessions
{
  "model": "llama3",
  "system_prompt": "...",
  "conversation_id": "conv_a1b2c3d4"  // optional
}
```

Response includes `conversation_id` field when auto-persist is active.

### JSONL Format

Each line is a JSON object representing one Message:

```jsonl
{"role":"system","content":"You are a helpful AI assistant."}
{"role":"user","content":"What is the capital of France?"}
{"role":"assistant","content":"The capital of France is Paris."}
{"role":"assistant","content":"","tool_calls":[{"id":"tc_1","name":"shell","arguments":{"command":"echo Paris"}}]}
{"role":"tool","content":"Paris\n","tool_call_id":"tc_1","name":"shell"}
```

**Export**: iterate messages, serialize each to one JSON line.

**Import**: parse each line, validate `role` field, create conversation + append messages.

### Error Handling

| Scenario | Handling |
|----------|----------|
| conv_store_->append() fails | Log warning, do not block conversation |
| conv_store_->create() fails | Log warning, conversation_id_ stays empty, no persist |
| conv_store_->load() fails (resume) | Log warning, start fresh conversation |
| conv_store_->load() fails (Server API) | Return 500 to client |
| Invalid conversation_id in /resume | Print "not found" |
| JSONL import parse error | Return 400 with line number |
| SQLite write failure | Log error, conversation continues in memory |
| conv_store_ is nullptr | Original AgentLoop behavior, zero impact |

**Core principle**: Persistence failures never block the main conversation flow.

### Configuration

```cpp
// src/config/Config.h addition
struct ConversationConfig {
    std::string path;             // conversations.db path (empty = auto)
    bool auto_resume = true;      // CLI: auto-resume last conversation
    bool auto_persist = true;     // Auto-save messages each turn
    int max_conversations = 1000; // Max stored conversations
};
```

TOML:

```toml
[conversation]
path = ""
auto_resume = true
auto_persist = true
max_conversations = 1000
```

### Namespace

`ea::conversation` — independent concern, not part of agent/memory/provider.

### File Structure

```
src/conversation/
  IConversationStore.h           — Interface + ConversationMeta
  SqliteConversationStore.h      — Class declaration
  SqliteConversationStore.cpp    — Implementation
  CMakeLists.txt                 — ea-conversation OBJECT library

src/agent/
  AgentLoop.h / .cpp             — Add conv_store_, conversation_id_, restore_conversation()

src/server/
  HttpServer.h / .cpp            — New /api/conversations/* endpoints
  SessionManager.h / .cpp        — conversation_id param + conv_store_ member

src/config/
  Config.h / .cpp                — ConversationConfig

src/main.cpp                     — Create ConversationStore, CLI commands, auto-resume

tests/
  test_conversation_store.cpp    — Unit tests (CRUD, JSONL, edge cases)
  test_conversation_integration.cpp — Integration tests (AgentLoop + restore, Server API)
```

### Dependencies

```
ea-conversation → ea-core (Types.h, Result.h, Error.h)
                → nlohmann/json
                → sqlite3

ea-agent → ea-conversation (new, optional via pointer)
ea-server → ea-conversation (new, optional via pointer)
```

### Test Plan

| Type | Coverage | Tags |
|------|----------|------|
| Unit | IConversationStore interface | `[conversation]` |
| Unit | SqliteConversationStore CRUD (create, append, load, list, remove) | `[conversation]` |
| Unit | Message serialization/deserialization (all Role types, tool_calls, optional fields) | `[conversation]` |
| Unit | JSONL export/import (round-trip, malformed input) | `[conversation]` |
| Unit | Edge cases (empty conversation, very long content, special characters) | `[conversation]` |
| Unit | Auto-cleanup when max_conversations exceeded | `[conversation]` |
| Integration | AgentLoop + ConversationStore (auto-persist, restore_conversation) | `[agent]` `[conversation]` |
| Integration | CLI auto-resume | `[agent]` `[conversation]` |
| Integration | Server /api/conversations/* endpoints | `[server]` `[conversation]` |
| Integration | Session creation with conversation_id | `[server]` `[conversation]` |

### Out of Scope

- Conversation search (full-text search across conversations)
- Conversation branching / forking
- Multi-user access control per conversation
- Conversation sharing between agents
- Streaming conversation export
- Conversation versioning / undo
