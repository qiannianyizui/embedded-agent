# Phase 2C: Memory System Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the memory system with lifecycle hooks (open/close, on_turn_start/end, on_pre_compress, system_prompt_block), MemoryManager orchestration layer, ScopedMemory agent isolation, and MemoryFactory backend selection.

**Architecture:** IMemory interface gains default-implemented lifecycle hooks. MemoryManager wraps an IMemory backend and orchestrates turn-level prefetch/sync. ScopedMemory decorates IMemory with agent-scoped isolation. MemoryFactory creates backends by enum type.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, SQLite3, Catch2

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/memory/MemoryManager.h` | Orchestration: prefetch, sync_turn, build_memory_block |
| `src/memory/MemoryManager.cpp` | Implementation |
| `src/memory/ScopedMemory.h` | Agent-scoped memory decorator |
| `src/memory/ScopedMemory.cpp` | Implementation |
| `src/memory/MemoryFactory.h` | Backend factory |
| `src/memory/MemoryFactory.cpp` | Implementation |
| `src/memory/InMemoryBackend.h` | In-memory backend for testing |
| `src/memory/InMemoryBackend.cpp` | Implementation |
| `src/memory/NullMemory.h` | Null/no-op backend |
| `tests/test_memory_manager.cpp` | MemoryManager tests |
| `tests/test_scoped_memory.cpp` | ScopedMemory tests |
| `tests/test_memory_factory.cpp` | MemoryFactory tests |
| `tests/test_in_memory_backend.cpp` | InMemoryBackend tests |

### Modified Files

| File | Change |
|------|--------|
| `src/core/IMemory.h` | Add lifecycle hooks with default implementations |
| `src/memory/SqliteMemory.h/.cpp` | Implement lifecycle hooks |
| `src/memory/CMakeLists.txt` | Add new source files |
| `tests/CMakeLists.txt` | Add new test files |

---

## Task 1: Extend IMemory with Lifecycle Hooks

**Files:**
- Modify: `src/core/IMemory.h`

- [ ] **Step 1: Add lifecycle hooks to IMemory**

Current IMemory has only CRUD methods. Add default-implemented hooks:

```cpp
#pragma once
#include "Types.h"
#include "common/base/Result.h"

namespace ea {

class IMemory {
public:
    virtual ~IMemory() = default;

    // Existing CRUD
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;

    // Lifecycle hooks (default no-op implementations)
    virtual Result<void> open() { return {}; }
    virtual Result<void> close() { return {}; }
    virtual std::string system_prompt_block() const { return {}; }

    // Turn-level hooks
    virtual void on_turn_start(const std::string& user_input) { (void)user_input; }
    virtual void on_turn_end(const std::string& assistant_output) { (void)assistant_output; }
    virtual void on_pre_compress() {}
};

}  // namespace ea
```

- [ ] **Step 2: Build and verify no regressions**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[memory]" --reporter compact`
Expected: All existing memory tests still pass.

- [ ] **Step 3: Commit**

```bash
git add src/core/IMemory.h
git commit -m "feat(memory): add lifecycle hooks to IMemory interface"
```

---

## Task 2: InMemoryBackend

**Files:**
- Create: `src/memory/InMemoryBackend.h`
- Create: `src/memory/InMemoryBackend.cpp`
- Create: `tests/test_in_memory_backend.cpp`
- Modify: `src/memory/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write InMemoryBackend header**

```cpp
// src/memory/InMemoryBackend.h
#pragma once
#include "core/IMemory.h"
#include <vector>
#include <string>

namespace ea::memory {

class InMemoryBackend : public IMemory {
public:
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    std::string system_prompt_block() const override;

private:
    std::vector<MemoryEntry> entries_;
    int next_id_ = 1;
};

}  // namespace ea::memory
```

- [ ] **Step 2: Write InMemoryBackend implementation**

```cpp
// src/memory/InMemoryBackend.cpp
#include "InMemoryBackend.h"
#include <sstream>

namespace ea::memory {

Result<std::string> InMemoryBackend::store(const std::string& content,
                                            const std::string& category,
                                            int importance) {
    MemoryEntry entry;
    entry.id = std::to_string(next_id_++);
    entry.content = content;
    entry.category = category;
    entry.importance = importance;
    entries_.push_back(std::move(entry));
    return std::to_string(next_id_ - 1);
}

Result<std::vector<MemoryEntry>> InMemoryBackend::recall(const std::string& query,
                                                          int limit) {
    // Simple substring match, return most recent first up to limit
    std::vector<MemoryEntry> results;
    for (auto it = entries_.rbegin(); it != entries_.rend() && static_cast<int>(results.size()) < limit; ++it) {
        if (it->content.find(query) != std::string::npos) {
            results.push_back(*it);
        }
    }
    return results;
}

Result<bool> InMemoryBackend::forget(const std::string& id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

Result<std::vector<MemoryEntry>> InMemoryBackend::list(int limit, int offset) {
    if (offset >= static_cast<int>(entries_.size())) {
        return std::vector<MemoryEntry>{};
    }
    auto start = entries_.begin() + offset;
    auto end = start + std::min(static_cast<int>(entries_.size()) - offset, limit);
    return std::vector<MemoryEntry>(start, end);
}

Result<int> InMemoryBackend::count() {
    return static_cast<int>(entries_.size());
}

std::string InMemoryBackend::system_prompt_block() const {
    if (entries_.empty()) return {};

    std::ostringstream ss;
    ss << "# Relevant Memories\n";
    for (const auto& entry : entries_) {
        ss << "- [" << entry.category << "] " << entry.content << "\n";
    }
    return ss.str();
}

}  // namespace ea::memory
```

- [ ] **Step 3: Write tests**

```cpp
// tests/test_in_memory_backend.cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("InMemoryBackend store and recall", "[memory][inmemory]") {
    InMemoryBackend mem;
    auto id = mem.store("test content about C++", "core", 7);
    REQUIRE(id.ok());
    auto results = mem.recall("C++");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "test content about C++");
}

TEST_CASE("InMemoryBackend forget", "[memory][inmemory]") {
    InMemoryBackend mem;
    auto id = mem.store("to be deleted", "core", 3);
    REQUIRE(id.ok());
    auto del = mem.forget(id.value());
    REQUIRE(del.ok());
    REQUIRE(del.value() == true);
    REQUIRE(mem.count().value() == 0);
}

TEST_CASE("InMemoryBackend count", "[memory][inmemory]") {
    InMemoryBackend mem;
    REQUIRE(mem.count().value() == 0);
    mem.store("a", "core", 5);
    mem.store("b", "core", 5);
    REQUIRE(mem.count().value() == 2);
}

TEST_CASE("InMemoryBackend system_prompt_block", "[memory][inmemory]") {
    InMemoryBackend mem;
    REQUIRE(mem.system_prompt_block().empty());

    mem.store("user prefers dark mode", "preference", 7);
    auto block = mem.system_prompt_block();
    REQUIRE(block.find("Relevant Memories") != std::string::npos);
    REQUIRE(block.find("dark mode") != std::string::npos);
}

TEST_CASE("InMemoryBackend recall with limit", "[memory][inmemory]") {
    InMemoryBackend mem;
    mem.store("entry 1", "core", 5);
    mem.store("entry 2", "core", 5);
    mem.store("entry 3", "core", 5);

    auto results = mem.recall("entry", 2);
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 2);
}
```

- [ ] **Step 4: Add files to CMakeLists**

In `src/memory/CMakeLists.txt`, add `InMemoryBackend.cpp` to the source list.

In `tests/CMakeLists.txt`, add `test_in_memory_backend.cpp` to the `ea-tests` source list.

- [ ] **Step 5: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[inmemory]" --reporter compact`
Expected: All 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/memory/InMemoryBackend.h src/memory/InMemoryBackend.cpp tests/test_in_memory_backend.cpp src/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(memory): add InMemoryBackend for testing and ephemeral use"
```

---

## Task 3: NullMemory

**Files:**
- Create: `src/memory/NullMemory.h`
- Modify: `src/memory/CMakeLists.txt`

- [ ] **Step 1: Write NullMemory header**

```cpp
// src/memory/NullMemory.h
#pragma once
#include "core/IMemory.h"

namespace ea::memory {

class NullMemory : public IMemory {
public:
    Result<std::string> store(const std::string&, const std::string& = "core", int = 5) override {
        return std::string("null");
    }
    Result<std::vector<MemoryEntry>> recall(const std::string&, int = 10) override {
        return std::vector<MemoryEntry>{};
    }
    Result<bool> forget(const std::string&) override {
        return true;
    }
    Result<std::vector<MemoryEntry>> list(int = 50, int = 0) override {
        return std::vector<MemoryEntry>{};
    }
    Result<int> count() override {
        return 0;
    }
};

}  // namespace ea::memory
```

This is header-only, no .cpp needed.

- [ ] **Step 2: Add to CMakeLists if needed**

NullMemory is header-only, so no source file to add. Just ensure the include path covers it.

- [ ] **Step 3: Build and verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc)`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/memory/NullMemory.h
git commit -m "feat(memory): add NullMemory no-op backend"
```

---

## Task 4: MemoryFactory

**Files:**
- Create: `src/memory/MemoryFactory.h`
- Create: `src/memory/MemoryFactory.cpp`
- Create: `tests/test_memory_factory.cpp`
- Modify: `src/memory/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write MemoryFactory header**

```cpp
// src/memory/MemoryFactory.h
#pragma once
#include "core/IMemory.h"
#include <memory>
#include <string>

namespace ea::memory {

enum class MemoryBackendType { Sqlite, InMemory, Null };

struct MemoryBackendConfig {
    std::string path = ":memory:";
    bool enable_fts5 = true;
    bool enable_wal = false;
};

std::unique_ptr<IMemory> create_backend(
    MemoryBackendType type,
    const MemoryBackendConfig& config = {});

}  // namespace ea::memory
```

- [ ] **Step 2: Write MemoryFactory implementation**

```cpp
// src/memory/MemoryFactory.cpp
#include "MemoryFactory.h"
#include "SqliteMemory.h"
#include "InMemoryBackend.h"
#include "NullMemory.h"

namespace ea::memory {

std::unique_ptr<IMemory> create_backend(
    MemoryBackendType type,
    const MemoryBackendConfig& config)
{
    switch (type) {
    case MemoryBackendType::Sqlite: {
        SqliteMemory::Config cfg;
        cfg.path = config.path;
        cfg.enable_fts5 = config.enable_fts5;
        cfg.enable_wal = config.enable_wal;
        return std::make_unique<SqliteMemory>(cfg);
    }
    case MemoryBackendType::InMemory:
        return std::make_unique<InMemoryBackend>();
    case MemoryBackendType::Null:
        return std::make_unique<NullMemory>();
    }
    return std::make_unique<NullMemory>();
}

}  // namespace ea::memory
```

- [ ] **Step 3: Write tests**

```cpp
// tests/test_memory_factory.cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/MemoryFactory.h"

using namespace ea::memory;

TEST_CASE("MemoryFactory creates InMemory backend", "[memory][factory]") {
    auto mem = create_backend(MemoryBackendType::InMemory);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
}

TEST_CASE("MemoryFactory creates Null backend", "[memory][factory]") {
    auto mem = create_backend(MemoryBackendType::Null);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
    REQUIRE(mem->count().value() == 0);
}

TEST_CASE("MemoryFactory creates Sqlite backend", "[memory][factory]") {
    MemoryBackendConfig cfg;
    cfg.path = ":memory:";
    auto mem = create_backend(MemoryBackendType::Sqlite, cfg);
    REQUIRE(mem != nullptr);
    auto id = mem->store("test", "core", 5);
    REQUIRE(id.ok());
}
```

- [ ] **Step 4: Add files to CMakeLists**

In `src/memory/CMakeLists.txt`, add `MemoryFactory.cpp` to the source list.

In `tests/CMakeLists.txt`, add `test_memory_factory.cpp` to the `ea-tests` source list.

- [ ] **Step 5: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[factory]" --reporter compact`
Expected: All 3 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/memory/MemoryFactory.h src/memory/MemoryFactory.cpp tests/test_memory_factory.cpp src/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(memory): add MemoryFactory with Sqlite/InMemory/Null backends"
```

---

## Task 5: ScopedMemory

**Files:**
- Create: `src/memory/ScopedMemory.h`
- Create: `src/memory/ScopedMemory.cpp`
- Create: `tests/test_scoped_memory.cpp`
- Modify: `src/memory/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write ScopedMemory header**

```cpp
// src/memory/ScopedMemory.h
#pragma once
#include "core/IMemory.h"
#include <set>
#include <string>

namespace ea::memory {

struct MemoryScope {
    std::string agent_id;
    std::string session_id;
    std::set<std::string> read_allowlist;
};

class ScopedMemory : public IMemory {
public:
    ScopedMemory(std::unique_ptr<IMemory> backend, MemoryScope scope);

    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) override;
    Result<int> count() override;

    // Lifecycle delegation
    Result<void> open() override;
    Result<void> close() override;
    std::string system_prompt_block() const override;
    void on_turn_start(const std::string& user_input) override;
    void on_turn_end(const std::string& assistant_output) override;
    void on_pre_compress() override;

private:
    bool can_read(const MemoryEntry& entry) const;

    std::unique_ptr<IMemory> backend_;
    MemoryScope scope_;
};

}  // namespace ea::memory
```

- [ ] **Step 2: Write ScopedMemory implementation**

```cpp
// src/memory/ScopedMemory.cpp
#include "ScopedMemory.h"

namespace ea::memory {

ScopedMemory::ScopedMemory(std::unique_ptr<IMemory> backend, MemoryScope scope)
    : backend_(std::move(backend)), scope_(std::move(scope)) {}

Result<std::string> ScopedMemory::store(const std::string& content,
                                         const std::string& category,
                                         int importance) {
    // Store with agent_id attached via category prefix
    std::string scoped_category = scope_.agent_id + ":" + category;
    return backend_->store(content, scoped_category, importance);
}

Result<std::vector<MemoryEntry>> ScopedMemory::recall(const std::string& query,
                                                       int limit) {
    auto result = backend_->recall(query, limit);
    if (!result.ok()) return result;

    // Filter to own agent's entries + allowlisted agents
    std::vector<MemoryEntry> filtered;
    for (const auto& entry : result.value()) {
        if (can_read(entry)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

Result<bool> ScopedMemory::forget(const std::string& id) {
    return backend_->forget(id);
}

Result<std::vector<MemoryEntry>> ScopedMemory::list(int limit, int offset) {
    auto result = backend_->list(limit, offset);
    if (!result.ok()) return result;

    std::vector<MemoryEntry> filtered;
    for (const auto& entry : result.value()) {
        if (can_read(entry)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

Result<int> ScopedMemory::count() {
    return backend_->count();
}

bool ScopedMemory::can_read(const MemoryEntry& entry) const {
    // Own agent's entries (category starts with our agent_id)
    if (entry.category.find(scope_.agent_id + ":") == 0) return true;
    // Allowlisted agents' entries
    for (const auto& allowed_id : scope_.read_allowlist) {
        if (entry.category.find(allowed_id + ":") == 0) return true;
    }
    // Entries without agent prefix (unscoped) are readable by all
    if (entry.category.find(":") == std::string::npos) return true;
    return false;
}

// Lifecycle delegation
Result<void> ScopedMemory::open() { return backend_->open(); }
Result<void> ScopedMemory::close() { return backend_->close(); }
std::string ScopedMemory::system_prompt_block() const { return backend_->system_prompt_block(); }
void ScopedMemory::on_turn_start(const std::string& user_input) { backend_->on_turn_start(user_input); }
void ScopedMemory::on_turn_end(const std::string& assistant_output) { backend_->on_turn_end(assistant_output); }
void ScopedMemory::on_pre_compress() { backend_->on_pre_compress(); }

}  // namespace ea::memory
```

- [ ] **Step 3: Write tests**

```cpp
// tests/test_scoped_memory.cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/ScopedMemory.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("ScopedMemory store attaches agent_id to category", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    auto id = scoped.store("my thought", "core", 5);
    REQUIRE(id.ok());

    // Verify the backend stored with scoped category
    // We can check by listing and inspecting the category
    auto list = scoped.list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 1);
    REQUIRE(list.value()[0].category == "agent-A:core");
}

TEST_CASE("ScopedMemory can read own agent entries", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    scoped.store("my thought", "core", 5);
    auto results = scoped.recall("thought");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
}

TEST_CASE("ScopedMemory blocks other agent entries", "[memory][scoped]") {
    // Create shared backend, store from agent-B, then read from agent-A
    auto backend = std::make_unique<InMemoryBackend>();

    // Store from agent-B directly into backend
    backend->store("agent-B secret", "agent-B:core", 5);
    backend->store("unscoped entry", "core", 5);

    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    auto results = scoped.recall("entry");
    REQUIRE(results.ok());
    // Should only see unscoped entry, not agent-B's
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "unscoped entry");
}

TEST_CASE("ScopedMemory read_allowlist allows cross-agent reads", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("agent-B shared", "agent-B:core", 5);
    backend->store("unscoped entry", "core", 5);

    MemoryScope scope;
    scope.agent_id = "agent-A";
    scope.read_allowlist = {"agent-B"};
    ScopedMemory scoped(std::move(backend), scope);

    auto results = scoped.recall("entry");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 2);
}

TEST_CASE("ScopedMemory lifecycle delegates to backend", "[memory][scoped]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryScope scope;
    scope.agent_id = "agent-A";
    ScopedMemory scoped(std::move(backend), scope);

    REQUIRE(scoped.open().ok());
    REQUIRE(scoped.close().ok());
}
```

- [ ] **Step 4: Add files to CMakeLists**

In `src/memory/CMakeLists.txt`, add `ScopedMemory.cpp` to the source list.

In `tests/CMakeLists.txt`, add `test_scoped_memory.cpp` to the `ea-tests` source list.

- [ ] **Step 5: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[scoped]" --reporter compact`
Expected: All 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/memory/ScopedMemory.h src/memory/ScopedMemory.cpp tests/test_scoped_memory.cpp src/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(memory): add ScopedMemory with agent-scoped isolation"
```

---

## Task 6: MemoryManager

**Files:**
- Create: `src/memory/MemoryManager.h`
- Create: `src/memory/MemoryManager.cpp`
- Create: `tests/test_memory_manager.cpp`
- Modify: `src/memory/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write MemoryManager header**

```cpp
// src/memory/MemoryManager.h
#pragma once
#include "core/IMemory.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::memory {

class MemoryManager {
public:
    explicit MemoryManager(std::unique_ptr<IMemory> backend);

    // Turn lifecycle
    std::vector<MemoryEntry> prefetch(const std::string& user_input);
    void sync_turn(const std::string& user_input,
                   const std::string& assistant_output);

    // Delegate to underlying IMemory
    Result<std::string> store(const std::string& content,
                               const std::string& category = "core",
                               int importance = 5);
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit = 10);
    Result<bool> forget(const std::string& id);
    Result<int> count();

    // System prompt injection
    std::string build_memory_block() const;

    // Lifecycle
    Result<void> open();
    Result<void> close();
    void on_pre_compress();

    // Access the underlying backend (for MemoryTool integration)
    IMemory* backend() const { return backend_.get(); }

private:
    std::unique_ptr<IMemory> backend_;
    std::vector<MemoryEntry> cached_context_;
    std::string last_user_input_;
};

}  // namespace ea::memory
```

- [ ] **Step 2: Write MemoryManager implementation**

```cpp
// src/memory/MemoryManager.cpp
#include "MemoryManager.h"
#include "common/io/Logger.h"

namespace ea::memory {

MemoryManager::MemoryManager(std::unique_ptr<IMemory> backend)
    : backend_(std::move(backend)) {}

std::vector<MemoryEntry> MemoryManager::prefetch(const std::string& user_input) {
    last_user_input_ = user_input;

    // Notify backend of turn start
    backend_->on_turn_start(user_input);

    // Recall relevant memories for this input
    auto result = backend_->recall(user_input);
    if (result.ok()) {
        cached_context_ = std::move(result.value());
    } else {
        cached_context_.clear();
        EA_WARN("Memory prefetch failed: {}", result.error().message);
    }

    return cached_context_;
}

void MemoryManager::sync_turn(const std::string& user_input,
                               const std::string& assistant_output) {
    // Notify backend of turn end (auto-store if configured)
    backend_->on_turn_end(assistant_output);

    // Clear cached context for next turn
    cached_context_.clear();
}

Result<std::string> MemoryManager::store(const std::string& content,
                                          const std::string& category,
                                          int importance) {
    return backend_->store(content, category, importance);
}

Result<std::vector<MemoryEntry>> MemoryManager::recall(const std::string& query,
                                                        int limit) {
    return backend_->recall(query, limit);
}

Result<bool> MemoryManager::forget(const std::string& id) {
    return backend_->forget(id);
}

Result<int> MemoryManager::count() {
    return backend_->count();
}

std::string MemoryManager::build_memory_block() const {
    return backend_->system_prompt_block();
}

Result<void> MemoryManager::open() {
    return backend_->open();
}

Result<void> MemoryManager::close() {
    return backend_->close();
}

void MemoryManager::on_pre_compress() {
    backend_->on_pre_compress();
}

}  // namespace ea::memory
```

- [ ] **Step 3: Write tests**

```cpp
// tests/test_memory_manager.cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/MemoryManager.h"
#include "memory/InMemoryBackend.h"

using namespace ea;
using namespace ea::memory;

TEST_CASE("MemoryManager prefetch returns relevant memories", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("C++ is great", "core", 5);
    backend->store("Python is nice", "core", 5);

    MemoryManager mgr(std::move(backend));
    auto results = mgr.prefetch("C++");
    REQUIRE(results.size() >= 1);
    REQUIRE(results[0].content.find("C++") != std::string::npos);
}

TEST_CASE("MemoryManager store and recall", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    auto id = mgr.store("test content", "core", 5);
    REQUIRE(id.ok());

    auto results = mgr.recall("test");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
}

TEST_CASE("MemoryManager forget", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    auto id = mgr.store("to delete", "core", 3);
    REQUIRE(id.ok());
    auto result = mgr.forget(id.value());
    REQUIRE(result.ok());
    REQUIRE(result.value() == true);
}

TEST_CASE("MemoryManager count", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.count().value() == 0);
    mgr.store("a", "core", 5);
    mgr.store("b", "core", 5);
    REQUIRE(mgr.count().value() == 2);
}

TEST_CASE("MemoryManager build_memory_block", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.build_memory_block().empty());

    mgr.store("important fact", "core", 7);
    auto block = mgr.build_memory_block();
    REQUIRE_FALSE(block.empty());
}

TEST_CASE("MemoryManager sync_turn clears cached context", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    backend->store("some memory", "core", 5);

    MemoryManager mgr(std::move(backend));
    auto results = mgr.prefetch("memory");
    REQUIRE(results.size() >= 1);

    mgr.sync_turn("user input", "assistant output");
    // After sync, next prefetch should work fresh
    auto results2 = mgr.prefetch("memory");
    REQUIRE(results2.size() >= 1);
}

TEST_CASE("MemoryManager open and close", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.open().ok());
    REQUIRE(mgr.close().ok());
}

TEST_CASE("MemoryManager backend accessor", "[memory][manager]") {
    auto backend = std::make_unique<InMemoryBackend>();
    MemoryManager mgr(std::move(backend));

    REQUIRE(mgr.backend() != nullptr);
}
```

- [ ] **Step 4: Add files to CMakeLists**

In `src/memory/CMakeLists.txt`, add `MemoryManager.cpp` to the source list.

In `tests/CMakeLists.txt`, add `test_memory_manager.cpp` to the `ea-tests` source list.

- [ ] **Step 5: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[manager]" --reporter compact`
Expected: All 8 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/memory/MemoryManager.h src/memory/MemoryManager.cpp tests/test_memory_manager.cpp src/memory/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(memory): add MemoryManager orchestration layer"
```

---

## Task 7: Enhance SqliteMemory with Lifecycle Hooks

**Files:**
- Modify: `src/memory/SqliteMemory.h`
- Modify: `src/memory/SqliteMemory.cpp`

- [ ] **Step 1: Implement lifecycle hooks in SqliteMemory**

In `src/memory/SqliteMemory.h`, add overrides:

```cpp
Result<void> open() override;
Result<void> close() override;
std::string system_prompt_block() const override;
void on_turn_end(const std::string& assistant_output) override;
void on_pre_compress() override;
```

In `src/memory/SqliteMemory.cpp`, implement:

```cpp
Result<void> SqliteMemory::open() {
    // DB is already opened in constructor; this is a no-op
    // but could be used for lazy initialization in the future
    return {};
}

Result<void> SqliteMemory::close() {
    // SQLite connection is managed via unique_ptr; this allows explicit cleanup
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    return {};
}

std::string SqliteMemory::system_prompt_block() const {
    if (!db_) return {};

    // Query recent high-importance memories
    sqlite3_stmt* stmt;
    const char* sql = "SELECT category, content FROM memories "
                       "WHERE importance >= 6 "
                       "ORDER BY created_at DESC LIMIT 5";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    std::ostringstream ss;
    ss << "# Relevant Memories\n";
    bool has_any = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        has_any = true;
        ss << "- [" << sqlite3_column_text(stmt, 0) << "] "
           << sqlite3_column_text(stmt, 1) << "\n";
    }
    sqlite3_finalize(stmt);

    return has_any ? ss.str() : std::string{};
}

void SqliteMemory::on_turn_end(const std::string& assistant_output) {
    // Auto-store could be implemented here when auto_memory is enabled
    // For now, this is a no-op placeholder
    (void)assistant_output;
}

void SqliteMemory::on_pre_compress() {
    // Before context compression, could store a summary
    // For now, this is a no-op placeholder
}
```

Note: Need to add `#include <sstream>` if not already present.

- [ ] **Step 2: Build and run memory tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[memory]" --reporter compact`
Expected: All memory tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/memory/SqliteMemory.h src/memory/SqliteMemory.cpp
git commit -m "feat(memory): implement lifecycle hooks in SqliteMemory"
```
