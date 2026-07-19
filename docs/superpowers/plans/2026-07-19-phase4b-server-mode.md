# Phase 4B: Server Mode (HTTP API) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an HTTP server mode that exposes the agent loop as a RESTful API with SSE streaming, supporting multiple independent sessions and approval management.

**Architecture:** A new `ea::server` module wraps `httplib::Server` (already a project dependency) with a `SessionManager` that pools independent `AgentLoop` instances. Each session has isolated memory via `ScopedMemory` and its own `PendingApprovalHandler`. SSE is used for streaming chat and approval notifications.

**Tech Stack:** C++17, cpp-httplib (httplib::Server), nlohmann/json, spdlog, existing ea::agent/ea::memory/ea::security modules

## Global Constraints

- Namespace: `ea::server`
- Test tag: `[server]`
- Error construction: use `Error` aggregate struct with factory methods (`Error::net()`, `Error::not_found()`, `Error::invalid_arg()`, etc.)
- Object library: `ea-server` as CMake OBJECT library, linked into `embedded-agent-core`
- Compile flags: use `ea_target_compile_options(target)` for project targets
- No new external dependencies — cpp-httplib already available
- SSE event format: `data: <json>\n\n`
- Error response format: `{"error": {"code": "...", "message": "..."}}`
- HTTP status codes: 400 (invalid params), 404 (not found), 409 (conflict/running), 429 (max sessions), 500 (internal)
- CORS header: `Access-Control-Allow-Origin: <cors_origin>` on all responses
- Thread safety: SessionManager mutex-protected; each AgentLoop runs in its own thread

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/server/ServerConfig.h` | ServerConfig struct (host, port, max_sessions, cors_origin, idle_timeout) |
| `src/server/SessionManager.h` | Session struct + SessionManager class declarations |
| `src/server/SessionManager.cpp` | Session lifecycle: create, get, remove, list, cleanup_idle |
| `src/server/HttpServer.h` | HttpServer class declaration |
| `src/server/HttpServer.cpp` | Route setup + all request handlers |
| `src/server/CMakeLists.txt` | ea-server OBJECT library |
| `src/config/Config.h` | Add ServerConfig to AppConfig |
| `src/config/Config.cpp` | Parse [server] section from TOML |
| `src/main.cpp` | Add EA_MODE_SERVER branch |
| `CMakeLists.txt` | Add ea-server subdirectory, link into core |
| `tests/test_session_manager.cpp` | SessionManager unit tests |
| `tests/test_http_server.cpp` | HttpServer integration tests |
| `tests/CMakeLists.txt` | Add new test files |

---

### Task 1: ServerConfig + CMake Infrastructure

**Files:**
- Create: `src/server/ServerConfig.h`
- Create: `src/server/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `add_subdirectory(src/server)`, link `ea-server` into `embedded-agent-core`)
- Modify: `src/config/Config.h` (add `ServerConfig` to `AppConfig`)
- Modify: `src/config/Config.cpp` (parse `[server]` TOML section)
- Test: `tests/test_config.cpp` (existing, will verify new config fields)

**Interfaces:**
- Consumes: `ea::config` namespace, TOML parsing
- Produces: `ea::server::ServerConfig` struct, `ea::config::ServerConfig` in `AppConfig`, `ea-server` CMake OBJECT library

- [ ] **Step 1: Create `src/server/ServerConfig.h`**

```cpp
// src/server/ServerConfig.h
#pragma once
#include <string>
#include <chrono>

namespace ea::server {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    std::chrono::seconds session_idle_timeout{3600};
};

}  // namespace ea::server
```

- [ ] **Step 2: Create `src/server/CMakeLists.txt`**

```cmake
add_library(ea-server OBJECT
    SessionManager.cpp
    HttpServer.cpp
)
ea_target_compile_options(ea-server)
target_include_directories(ea-server PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-server PUBLIC spdlog::spdlog PRIVATE httplib::httplib)
```

Note: SessionManager.cpp and HttpServer.cpp don't exist yet — create empty stubs so CMake doesn't fail:

```cpp
// src/server/SessionManager.cpp — stub
// src/server/HttpServer.cpp — stub
```

- [ ] **Step 3: Add `add_subdirectory(src/server)` to root `CMakeLists.txt`**

After `add_subdirectory(src/mcp)`, add:

```cmake
add_subdirectory(src/server)
```

- [ ] **Step 4: Add `ea-server` objects to `embedded-agent-core` in root `CMakeLists.txt`**

In the `add_library(embedded-agent-core STATIC ...)` block, add `$<TARGET_OBJECTS:ea-server>` to the source list.

- [ ] **Step 5: Add `ServerConfig` to `src/config/Config.h`**

Add before `AppConfig`:

```cpp
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    int session_idle_timeout = 3600;  // seconds
};
```

Add `ServerConfig server;` field to `AppConfig`.

- [ ] **Step 6: Parse `[server]` section in `src/config/Config.cpp`**

After the `[mcp]` parsing block, add:

```cpp
if (data.contains("server")) {
    auto server = toml::find(data, "server");
    cfg.server.host = toml::find_or<std::string>(server, "host", cfg.server.host);
    cfg.server.port = toml::find_or<int>(server, "port", cfg.server.port);
    cfg.server.max_sessions = toml::find_or<int>(server, "max_sessions", cfg.server.max_sessions);
    cfg.server.cors_origin = toml::find_or<std::string>(server, "cors_origin", cfg.server.cors_origin);
    cfg.server.session_idle_timeout = toml::find_or<int>(server, "session_idle_timeout", cfg.server.session_idle_timeout);
}
```

- [ ] **Step 7: Create stub files for CMake**

Create empty `src/server/SessionManager.cpp` and `src/server/HttpServer.cpp` with just namespace comments:

```cpp
// src/server/SessionManager.cpp
#include "SessionManager.h"

namespace ea::server {
// Implementation in Task 2
}  // namespace ea::server
```

```cpp
// src/server/HttpServer.cpp
#include "HttpServer.h"

namespace ea::server {
// Implementation in Task 3
}  // namespace ea::server
```

- [ ] **Step 8: Build and verify**

Run: `cd build && cmake --build . -j$(nproc)`
Expected: Clean build with no errors

- [ ] **Step 9: Commit**

```bash
git add src/server/ CMakeLists.txt src/config/Config.h src/config/Config.cpp
git commit -m "feat: add ServerConfig and ea-server CMake infrastructure"
```

---

### Task 2: SessionManager

**Files:**
- Create: `src/server/SessionManager.h`
- Modify: `src/server/SessionManager.cpp` (replace stub)
- Create: `tests/test_session_manager.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Consumes: `ea::agent::AgentLoop`, `ea::IProvider`, `ea::tool::ToolRegistry`, `ea::IMemory`, `ea::security::SecurityPolicy`, `ea::memory::ScopedMemory`, `ea::memory::SqliteMemory`, `ea::server::ServerConfig`
- Produces: `ea::server::Session` struct, `ea::server::SessionManager` class with `create()`, `get()`, `remove()`, `list()`, `cleanup_idle()`

- [ ] **Step 1: Write failing tests for SessionManager**

Create `tests/test_session_manager.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "server/SessionManager.h"
#include "memory/InMemoryBackend.h"
#include "memory/ScopedMemory.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "core/IProvider.h"
#include "core/Types.h"

using namespace ea;
using namespace ea::server;
using namespace ea::memory;

// Minimal mock provider for testing
class MockProvider : public IProvider {
public:
    std::string name() const override { return "mock"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

TEST_CASE("SessionManager creates session with unique ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s1->id != s2->id);
    REQUIRE_FALSE(s1->id.empty());
}

TEST_CASE("SessionManager get returns session by ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* created = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* found = mgr.get(created->id);

    REQUIRE(found != nullptr);
    REQUIRE(found->id == created->id);
}

TEST_CASE("SessionManager get returns nullptr for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE(mgr.get("nonexistent") == nullptr);
}

TEST_CASE("SessionManager remove deletes session", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;
    REQUIRE(mgr.remove(id));
    REQUIRE(mgr.get(id) == nullptr);
}

TEST_CASE("SessionManager remove returns false for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE_FALSE(mgr.remove("nonexistent"));
}

TEST_CASE("SessionManager list returns all sessions", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    auto list = mgr.list();
    REQUIRE(list.size() == 2);
}

TEST_CASE("SessionManager respects max_sessions limit", "[server][session]") {
    ServerConfig cfg;
    cfg.max_sessions = 2;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s3 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s3 == nullptr);  // Exceeds max_sessions
}

TEST_CASE("SessionManager cleanup_idle removes expired sessions", "[server][session]") {
    ServerConfig cfg;
    cfg.session_idle_timeout = 0;  // Immediate timeout
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;

    // Set last_active far in the past
    s->last_active = std::chrono::steady_clock::now() - std::chrono::hours(2);

    mgr.cleanup_idle();
    REQUIRE(mgr.get(id) == nullptr);
}
```

- [ ] **Step 2: Add test file to `tests/CMakeLists.txt`**

Add `test_session_manager.cpp` to the `add_executable(ea-tests ...)` source list.

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[session]"`
Expected: FAIL — SessionManager.h not found

- [ ] **Step 4: Create `src/server/SessionManager.h`**

```cpp
// src/server/SessionManager.h
#pragma once
#include "ServerConfig.h"
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "security/SecurityPolicy.h"
#include "memory/ScopedMemory.h"
#include "memory/InMemoryBackend.h"
#include "security/PendingApprovalHandler.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>

namespace ea::server {

struct Session {
    std::string id;
    std::shared_ptr<AgentLoop> loop;
    std::unique_ptr<IMemory> memory;       // ScopedMemory wrapping backend
    std::unique_ptr<security::PendingApprovalHandler> approval;
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point last_active;
    std::string model;                     // model override for this session
};

class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config);

    Session* create(IProvider* provider,
                    tool::ToolRegistry* registry,
                    IMemory* shared_backend,
                    security::SecurityPolicy* policy,
                    const AgentLoop::Config& loop_cfg,
                    const std::string& model = "",
                    const std::string& system_prompt = "");

    Session* get(const std::string& id);
    bool remove(const std::string& id);
    std::vector<Session*> list();
    void cleanup_idle();

private:
    std::string generate_id();

    ServerConfig config_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace ea::server
```

- [ ] **Step 5: Implement `src/server/SessionManager.cpp`**

```cpp
// src/server/SessionManager.cpp
#include "SessionManager.h"
#include "common/io/Logger.h"
#include <sstream>
#include <iomanip>

namespace ea::server {

SessionManager::SessionManager(const ServerConfig& config)
    : config_(config) {}

std::string SessionManager::generate_id() {
    // Generate "sess_" + 8 hex chars from random bytes
    std::lock_guard<std::mutex> lock(mutex_);
    std::stringstream ss;
    ss << "sess_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << (rng_() & 0xFF);
    }
    return ss.str();
}

Session* SessionManager::create(IProvider* provider,
                                 tool::ToolRegistry* registry,
                                 IMemory* shared_backend,
                                 security::SecurityPolicy* policy,
                                 const AgentLoop::Config& loop_cfg,
                                 const std::string& model,
                                 const std::string& system_prompt) {
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
    if (shared_backend) {
        // Wrap shared backend with ScopedMemory for isolation
        // Note: we create a new InMemoryBackend per session for simplicity
        // In production, shared_backend would be a shared SqliteMemory
        auto per_session_backend = std::make_unique<memory::InMemoryBackend>();
        session->memory = std::make_unique<memory::ScopedMemory>(
            std::move(per_session_backend), scope);
    } else {
        auto per_session_backend = std::make_unique<memory::InMemoryBackend>();
        session->memory = std::make_unique<memory::ScopedMemory>(
            std::move(per_session_backend), scope);
    }

    // Create per-session approval handler
    session->approval = std::make_unique<security::PendingApprovalHandler>(
        300);  // 5 minute default timeout

    // Create AgentLoop for this session
    auto effective_model = model.empty() ? "" : model;

    session->loop = std::make_shared<AgentLoop>(
        provider,
        registry,
        session->memory.get(),
        loop_cfg,
        [](const std::string& text) { (void)text; },  // Output captured via history
        nullptr,  // StreamFn set per-request
        policy,
        session->approval.get()
    );

    auto* ptr = session.get();
    sessions_[session->id] = std::move(session);

    EA_INFO("Session created: {}", ptr->id);
    return ptr;
}

Session* SessionManager::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    it->second->last_active = std::chrono::steady_clock::now();
    return it->second.get();
}

bool SessionManager::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;

    EA_INFO("Session removed: {}", id);
    sessions_.erase(it);
    return true;
}

std::vector<Session*> SessionManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Session*> result;
    for (const auto& [id, session] : sessions_) {
        result.push_back(session.get());
    }
    return result;
}

void SessionManager::cleanup_idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(config_.session_idle_timeout);

    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second->last_active);
        if (elapsed > timeout && !it->second->running.load()) {
            EA_INFO("Session expired (idle): {}", it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace ea::server
```

- [ ] **Step 6: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[session]"`
Expected: All 7 session tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/server/SessionManager.h src/server/SessionManager.cpp tests/test_session_manager.cpp tests/CMakeLists.txt
git commit -m "feat: implement SessionManager with create/get/remove/list/cleanup_idle"
```

---

### Task 3: HttpServer Core + Session Endpoints

**Files:**
- Create: `src/server/HttpServer.h`
- Modify: `src/server/HttpServer.cpp` (replace stub)
- Create: `tests/test_http_server.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Consumes: `ea::server::SessionManager`, `ea::server::ServerConfig`, `ea::IProvider`, `ea::tool::ToolRegistry`, `ea::security::SecurityPolicy`, `httplib::Server`
- Produces: `ea::server::HttpServer` class with `start()`, `stop()`

- [ ] **Step 1: Write failing tests for HttpServer session endpoints**

Create `tests/test_http_server.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "server/HttpServer.h"
#include "server/ServerConfig.h"
#include "memory/InMemoryBackend.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "core/IProvider.h"
#include "core/Types.h"
#include <httplib.h>
#include <thread>
#include <chrono>

using namespace ea;
using namespace ea::server;
using namespace ea::memory;

class MockProvider : public IProvider {
public:
    std::string name() const override { return "mock"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

// Helper: start server on a random available port
struct ServerFixture {
    std::shared_ptr<MockProvider> provider;
    std::shared_ptr<tool::ToolRegistry> registry;
    std::shared_ptr<security::SecurityPolicy> policy;
    std::shared_ptr<InMemoryBackend> backend;
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
    int port;

    ServerFixture() {
        provider = std::make_shared<MockProvider>();
        registry = std::make_shared<tool::ToolRegistry>();
        policy = std::make_shared<security::SecurityPolicy>();
        backend = std::make_shared<InMemoryBackend>();

        ServerConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 0;  // Let OS pick a port
        server = std::make_unique<HttpServer>(cfg, provider.get(), registry.get(), policy.get(), backend.get());

        // Start server in background
        server_thread = std::thread([this]() { server->start(); });

        // Wait for server to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        port = server->bound_port();
    }

    ~ServerFixture() {
        server->stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }
};

TEST_CASE("Health endpoint returns ok", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["status"] == "ok");
    REQUIRE(body.contains("uptime"));
    REQUIRE(body.contains("sessions"));
}

TEST_CASE("Create session returns session ID", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["model"] = "mock-model";

    auto res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("id"));
    REQUIRE(body["id"].get<std::string>().substr(0, 5) == "sess_");
}

TEST_CASE("List sessions returns array", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create a session first
    json req;
    cli.Post("/api/sessions", req.dump(), "application/json");

    auto res = cli.Get("/api/sessions");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
    REQUIRE(body[0].contains("id"));
    REQUIRE(body[0].contains("running"));
}

TEST_CASE("Delete session removes it", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    auto create_res = cli.Post("/api/sessions", req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto del_res = cli.Delete("/api/sessions/" + id);
    REQUIRE(del_res != nullptr);
    REQUIRE(del_res->status == 200);

    auto del_body = json::parse(del_res->body);
    REQUIRE(del_body["ok"] == true);

    // Verify it's gone
    auto list_res = cli.Get("/api/sessions");
    auto list_body = json::parse(list_res->body);
    bool found = false;
    for (const auto& s : list_body) {
        if (s["id"] == id) found = true;
    }
    REQUIRE_FALSE(found);
}

TEST_CASE("Delete nonexistent session returns 404", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Delete("/api/sessions/nonexistent");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Chat on nonexistent session returns 404", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/nonexistent/chat", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Models endpoint returns list", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/models");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
}

TEST_CASE("CORS headers present", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->has_header("Access-Control-Allow-Origin"));
}
```

- [ ] **Step 2: Add test file to `tests/CMakeLists.txt`**

Add `test_http_server.cpp` to the `add_executable(ea-tests ...)` source list.

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[http]"`
Expected: FAIL — HttpServer.h not found

- [ ] **Step 4: Create `src/server/HttpServer.h`**

```cpp
// src/server/HttpServer.h
#pragma once
#include "ServerConfig.h"
#include "SessionManager.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "security/SecurityPolicy.h"
#include <string>
#include <chrono>

namespace httplib { class Server; }

namespace ea::server {

class HttpServer {
public:
    HttpServer(ServerConfig config,
               IProvider* provider,
               tool::ToolRegistry* registry,
               security::SecurityPolicy* policy,
               IMemory* shared_memory = nullptr);
    ~HttpServer();

    void start();   // blocking
<thinking>
I need to continue writing the implementation plan. Let me continue from where I left off with the HttpServer.h file and the rest of the plan.</think>继续撰写实施计划。<tool_call>Write<arg_key>content</arg_key><arg_value># Phase 4B: Server Mode (HTTP API) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an HTTP server mode that exposes the agent loop as a RESTful API with SSE streaming, supporting multiple independent sessions and approval management.

**Architecture:** A new `ea::server` module wraps `httplib::Server` (already a project dependency) with a `SessionManager` that pools independent `AgentLoop` instances. Each session has isolated memory via `ScopedMemory` and its own `PendingApprovalHandler`. SSE is used for streaming chat and approval notifications.

**Tech Stack:** C++17, cpp-httplib (httplib::Server), nlohmann/json, spdlog, existing ea::agent/ea::memory/ea::security modules

## Global Constraints

- Namespace: `ea::server`
- Test tag: `[server]`
- Error construction: use `Error` aggregate struct with factory methods (`Error::net()`, `Error::not_found()`, `Error::invalid_arg()`, etc.)
- Object library: `ea-server` as CMake OBJECT library, linked into `embedded-agent-core`
- Compile flags: use `ea_target_compile_options(target)` for project targets
- No new external dependencies — cpp-httplib already available
- SSE event format: `data: <json>\n\n`
- Error response format: `{"error": {"code": "...", "message": "..."}}`
- HTTP status codes: 400 (invalid params), 404 (not found), 409 (conflict/running), 429 (max sessions), 500 (internal)
- CORS header: `Access-Control-Allow-Origin: <cors_origin>` on all responses
- Thread safety: SessionManager mutex-protected; each AgentLoop runs in its own thread

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/server/ServerConfig.h` | ServerConfig struct (host, port, max_sessions, cors_origin, idle_timeout) |
| `src/server/SessionManager.h` | Session struct + SessionManager class declarations |
| `src/server/SessionManager.cpp` | Session lifecycle: create, get, remove, list, cleanup_idle |
| `src/server/HttpServer.h` | HttpServer class declaration |
| `src/server/HttpServer.cpp` | Route setup + all request handlers |
| `src/server/CMakeLists.txt` | ea-server OBJECT library |
| `src/config/Config.h` | Add ServerConfig to AppConfig |
| `src/config/Config.cpp` | Parse [server] section from TOML |
| `src/main.cpp` | Add EA_MODE_SERVER branch |
| `CMakeLists.txt` | Add ea-server subdirectory, link into core |
| `tests/test_session_manager.cpp` | SessionManager unit tests |
| `tests/test_http_server.cpp` | HttpServer integration tests |
| `tests/CMakeLists.txt` | Add new test files |

---

### Task 1: ServerConfig + CMake Infrastructure

**Files:**
- Create: `src/server/ServerConfig.h`
- Create: `src/server/CMakeLists.txt`
- Create: `src/server/SessionManager.cpp` (stub)
- Create: `src/server/HttpServer.cpp` (stub)
- Modify: `CMakeLists.txt` (add `add_subdirectory(src/server)`, link `ea-server` into `embedded-agent-core`)
- Modify: `src/config/Config.h` (add `ServerConfig` to `AppConfig`)
- Modify: `src/config/Config.cpp` (parse `[server]` TOML section)

**Interfaces:**
- Consumes: `ea::config` namespace, TOML parsing
- Produces: `ea::server::ServerConfig` struct, `ea::config::ServerConfig` in `AppConfig`, `ea-server` CMake OBJECT library

- [ ] **Step 1: Create `src/server/ServerConfig.h`**

```cpp
// src/server/ServerConfig.h
#pragma once
#include <string>
#include <chrono>

namespace ea::server {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    int session_idle_timeout = 3600;  // seconds
};

}  // namespace ea::server
```

- [ ] **Step 2: Create `src/server/CMakeLists.txt`**

```cmake
add_library(ea-server OBJECT
    SessionManager.cpp
    HttpServer.cpp
)
ea_target_compile_options(ea-server)
target_include_directories(ea-server PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-server PUBLIC spdlog::spdlog PRIVATE httplib::httplib)
```

- [ ] **Step 3: Create stub `src/server/SessionManager.cpp`**

```cpp
// src/server/SessionManager.cpp
#include "SessionManager.h"

namespace ea::server {
// Implementation in Task 2
}  // namespace ea::server
```

- [ ] **Step 4: Create stub `src/server/HttpServer.cpp`**

```cpp
// src/server/HttpServer.cpp
#include "HttpServer.h"

namespace ea::server {
// Implementation in Task 3
}  // namespace ea::server
```

- [ ] **Step 5: Add `add_subdirectory(src/server)` to root `CMakeLists.txt`**

After `add_subdirectory(src/mcp)`, add:

```cmake
add_subdirectory(src/server)
```

- [ ] **Step 6: Add `ea-server` objects to `embedded-agent-core` in root `CMakeLists.txt`**

In the `add_library(embedded-agent-core STATIC ...)` source list, add `$<TARGET_OBJECTS:ea-server>`.

- [ ] **Step 7: Add `ServerConfig` to `src/config/Config.h`**

Add before `AppConfig`:

```cpp
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    int session_idle_timeout = 3600;  // seconds
};
```

Add `ServerConfig server;` field to `AppConfig`.

- [ ] **Step 8: Parse `[server]` section in `src/config/Config.cpp`**

After the `[mcp]` parsing block, add:

```cpp
if (data.contains("server")) {
    auto server = toml::find(data, "server");
    cfg.server.host = toml::find_or<std::string>(server, "host", cfg.server.host);
    cfg.server.port = toml::find_or<int>(server, "port", cfg.server.port);
    cfg.server.max_sessions = toml::find_or<int>(server, "max_sessions", cfg.server.max_sessions);
    cfg.server.cors_origin = toml::find_or<std::string>(server, "cors_origin", cfg.server.cors_origin);
    cfg.server.session_idle_timeout = toml::find_or<int>(server, "session_idle_timeout", cfg.server.session_idle_timeout);
}
```

- [ ] **Step 9: Build and verify**

Run: `cd build && cmake --build . -j$(nproc)`
Expected: Clean build with no errors

- [ ] **Step 10: Commit**

```bash
git add src/server/ CMakeLists.txt src/config/Config.h src/config/Config.cpp
git commit -m "feat: add ServerConfig and ea-server CMake infrastructure"
```

---

### Task 2: SessionManager

**Files:**
- Create: `src/server/SessionManager.h`
- Modify: `src/server/SessionManager.cpp` (replace stub)
- Create: `tests/test_session_manager.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Consumes: `ea::agent::AgentLoop`, `ea::IProvider`, `ea::tool::ToolRegistry`, `ea::IMemory`, `ea::security::SecurityPolicy`, `ea::memory::ScopedMemory`, `ea::memory::InMemoryBackend`, `ea::security::PendingApprovalHandler`, `ea::server::ServerConfig`
- Produces: `ea::server::Session` struct, `ea::server::SessionManager` class with `create()`, `get()`, `remove()`, `list()`, `cleanup_idle()`

- [ ] **Step 1: Write failing tests for SessionManager**

Create `tests/test_session_manager.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "server/SessionManager.h"
#include "memory/InMemoryBackend.h"
#include "memory/ScopedMemory.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "core/IProvider.h"
#include "core/Types.h"

using namespace ea;
using namespace ea::server;
using namespace ea::memory;

// Minimal mock provider for testing
class MockProvider : public IProvider {
public:
    std::string name() const override { return "mock"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

TEST_CASE("SessionManager creates session with unique ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s1->id != s2->id);
    REQUIRE_FALSE(s1->id.empty());
}

TEST_CASE("SessionManager get returns session by ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* created = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* found = mgr.get(created->id);

    REQUIRE(found != nullptr);
    REQUIRE(found->id == created->id);
}

TEST_CASE("SessionManager get returns nullptr for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE(mgr.get("nonexistent") == nullptr);
}

TEST_CASE("SessionManager remove deletes session", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;
    REQUIRE(mgr.remove(id));
    REQUIRE(mgr.get(id) == nullptr);
}

TEST_CASE("SessionManager remove returns false for unknown ID", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);

    REQUIRE_FALSE(mgr.remove("nonexistent"));
}

TEST_CASE("SessionManager list returns all sessions", "[server][session]") {
    ServerConfig cfg;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    auto list = mgr.list();
    REQUIRE(list.size() == 2);
}

TEST_CASE("SessionManager respects max_sessions limit", "[server][session]") {
    ServerConfig cfg;
    cfg.max_sessions = 2;
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s1 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s2 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    auto* s3 = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});

    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    REQUIRE(s3 == nullptr);  // Exceeds max_sessions
}

TEST_CASE("SessionManager cleanup_idle removes expired sessions", "[server][session]") {
    ServerConfig cfg;
    cfg.session_idle_timeout = 0;  // Immediate timeout
    SessionManager mgr(cfg);
    auto provider = std::make_shared<MockProvider>();
    auto registry = std::make_shared<tool::ToolRegistry>();
    auto policy = std::make_shared<security::SecurityPolicy>();
    auto backend = std::make_shared<InMemoryBackend>();

    auto* s = mgr.create(provider.get(), registry.get(), backend.get(), policy.get(), {});
    std::string id = s->id;

    // Set last_active far in the past
    s->last_active = std::chrono::steady_clock::now() - std::chrono::hours(2);

    mgr.cleanup_idle();
    REQUIRE(mgr.get(id) == nullptr);
}
```

- [ ] **Step 2: Add test file to `tests/CMakeLists.txt`**

Add `test_session_manager.cpp` to the `add_executable(ea-tests ...)` source list.

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[session]"`
Expected: FAIL — SessionManager.h not found

- [ ] **Step 4: Create `src/server/SessionManager.h`**

```cpp
// src/server/SessionManager.h
#pragma once
#include "ServerConfig.h"
#include "agent/AgentLoop.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "security/SecurityPolicy.h"
#include "memory/ScopedMemory.h"
#include "memory/InMemoryBackend.h"
#include "security/PendingApprovalHandler.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>

namespace ea::server {

struct Session {
    std::string id;
    std::shared_ptr<AgentLoop> loop;
    std::unique_ptr<IMemory> memory;       // ScopedMemory wrapping backend
    std::unique_ptr<security::PendingApprovalHandler> approval;
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point last_active;
    std::string model;                     // model override for this session
};

class SessionManager {
public:
    explicit SessionManager(const ServerConfig& config);

    Session* create(IProvider* provider,
                    tool::ToolRegistry* registry,
                    IMemory* shared_backend,
                    security::SecurityPolicy* policy,
                    const AgentLoop::Config& loop_cfg,
                    const std::string& model = "",
                    const std::string& system_prompt = "");

    Session* get(const std::string& id);
    bool remove(const std::string& id);
    std::vector<Session*> list();
    void cleanup_idle();

private:
    std::string generate_id();

    ServerConfig config_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace ea::server
```

- [ ] **Step 5: Implement `src/server/SessionManager.cpp`**

```cpp
// src/server/SessionManager.cpp
#include "SessionManager.h"
#include "common/io/Logger.h"
#include <sstream>
#include <iomanip>

namespace ea::server {

SessionManager::SessionManager(const ServerConfig& config)
    : config_(config) {}

std::string SessionManager::generate_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::stringstream ss;
    ss << "sess_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << (rng_() & 0xFF);
    }
    return ss.str();
}

Session* SessionManager::create(IProvider* provider,
                                 tool::ToolRegistry* registry,
                                 IMemory* /*shared_backend*/,
                                 security::SecurityPolicy* policy,
                                 const AgentLoop::Config& loop_cfg,
                                 const std::string& model,
                                 const std::string& /*system_prompt*/) {
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

    // Create AgentLoop for this session
    session->loop = std::make_shared<AgentLoop>(
        provider,
        registry,
        session->memory.get(),
        loop_cfg,
        [](const std::string& text) { (void)text; },  // Output captured via history
        nullptr,  // StreamFn set per-request
        policy,
        session->approval.get()
    );

    auto* ptr = session.get();
    sessions_[session->id] = std::move(session);

    EA_INFO("Session created: {}", ptr->id);
    return ptr;
}

Session* SessionManager::get(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    it->second->last_active = std::chrono::steady_clock::now();
    return it->second.get();
}

bool SessionManager::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;

    EA_INFO("Session removed: {}", id);
    sessions_.erase(it);
    return true;
}

std::vector<Session*> SessionManager::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Session*> result;
    for (const auto& [id, session] : sessions_) {
        result.push_back(session.get());
    }
    return result;
}

void SessionManager::cleanup_idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(config_.session_idle_timeout);

    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second->last_active);
        if (elapsed > timeout && !it->second->running.load()) {
            EA_INFO("Session expired (idle): {}", it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace ea::server
```

- [ ] **Step 6: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[session]"`
Expected: All 7 session tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/server/SessionManager.h src/server/SessionManager.cpp tests/test_session_manager.cpp tests/CMakeLists.txt
git commit -m "feat: implement SessionManager with create/get/remove/list/cleanup_idle"
```

---

### Task 3: HttpServer Core + Session & System Endpoints

**Files:**
- Create: `src/server/HttpServer.h`
- Modify: `src/server/HttpServer.cpp` (replace stub)
- Create: `tests/test_http_server.cpp`
- Modify: `tests/CMakeLists.txt` (add test file)

**Interfaces:**
- Consumes: `ea::server::SessionManager`, `ea::server::ServerConfig`, `ea::IProvider`, `ea::tool::ToolRegistry`, `ea::security::SecurityPolicy`, `ea::IMemory`, `httplib::Server`
- Produces: `ea::server::HttpServer` class with `start()`, `stop()`, `bound_port()`

This task implements: health, models, session CRUD, CORS, and the error response helper. Chat/stream/approval endpoints come in Task 4.

- [ ] **Step 1: Write failing tests for HttpServer basic endpoints**

Create `tests/test_http_server.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "server/HttpServer.h"
#include "server/ServerConfig.h"
#include "memory/InMemoryBackend.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "core/IProvider.h"
#include "core/Types.h"
#include <httplib.h>
#include <thread>
#include <chrono>

using namespace ea;
using namespace ea::server;
using namespace ea::memory;

class MockProvider : public IProvider {
public:
    std::string name() const override { return "mock"; }
    std::vector<std::string> list_models() const override { return {"mock-model"}; }
    provider::ProviderCapabilities capabilities() const override { return {true, true, false, false, false}; }
    Result<LLMResponse> chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, const ChatOptions&) override {
        return LLMResponse{"mock response", {}, {}, "stop"};
    }
    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override { return {}; }
};

// Helper: start server on a random available port
struct ServerFixture {
    std::shared_ptr<MockProvider> provider;
    std::shared_ptr<tool::ToolRegistry> registry;
    std::shared_ptr<security::SecurityPolicy> policy;
    std::shared_ptr<InMemoryBackend> backend;
    std::unique_ptr<HttpServer> server;
    std::thread server_thread;
    int port;

    ServerFixture() {
        provider = std::make_shared<MockProvider>();
        registry = std::make_shared<tool::ToolRegistry>();
        policy = std::make_shared<security::SecurityPolicy>();
        backend = std::make_shared<InMemoryBackend>();

        ServerConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 0;  // Let OS pick a port
        server = std::make_unique<HttpServer>(cfg, provider.get(), registry.get(), policy.get(), backend.get());

        server_thread = std::thread([this]() { server->start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        port = server->bound_port();
    }

    ~ServerFixture() {
        server->stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }
};

TEST_CASE("Health endpoint returns ok", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["status"] == "ok");
    REQUIRE(body.contains("uptime"));
    REQUIRE(body.contains("sessions"));
}

TEST_CASE("Create session returns session ID", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["model"] = "mock-model";

    auto res = cli.Post("/api/sessions", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("id"));
    REQUIRE(body["id"].get<std::string>().substr(0, 5) == "sess_");
}

TEST_CASE("List sessions returns array", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    cli.Post("/api/sessions", req.dump(), "application/json");

    auto res = cli.Get("/api/sessions");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
    REQUIRE(body[0].contains("id"));
    REQUIRE(body[0].contains("running"));
}

TEST_CASE("Delete session removes it", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    auto create_res = cli.Post("/api/sessions", req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto del_res = cli.Delete("/api/sessions/" + id);
    REQUIRE(del_res != nullptr);
    REQUIRE(del_res->status == 200);

    auto del_body = json::parse(del_res->body);
    REQUIRE(del_body["ok"] == true);

    // Verify it's gone
    auto list_res = cli.Get("/api/sessions");
    auto list_body = json::parse(list_res->body);
    bool found = false;
    for (const auto& s : list_body) {
        if (s["id"] == id) found = true;
    }
    REQUIRE_FALSE(found);
}

TEST_CASE("Delete nonexistent session returns 404", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Delete("/api/sessions/nonexistent");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Models endpoint returns list", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/models");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
    REQUIRE(body.size() >= 1);
}

TEST_CASE("CORS headers present", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/health");
    REQUIRE(res != nullptr);
    REQUIRE(res->has_header("Access-Control-Allow-Origin"));
}
```

- [ ] **Step 2: Add test file to `tests/CMakeLists.txt`**

Add `test_http_server.cpp` to the `add_executable(ea-tests ...)` source list.

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[http]"`
Expected: FAIL — HttpServer.h not found

- [ ] **Step 4: Create `src/server/HttpServer.h`**

```cpp
// src/server/HttpServer.h
#pragma once
#include "ServerConfig.h"
#include "SessionManager.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "security/SecurityPolicy.h"
#include <string>
#include <chrono>

namespace httplib { class Server; }

namespace ea::server {

class HttpServer {
public:
    HttpServer(ServerConfig config,
               IProvider* provider,
               tool::ToolRegistry* registry,
               security::SecurityPolicy* policy,
               IMemory* shared_memory = nullptr);
    ~HttpServer();

    void start();   // blocking
    void stop();    // graceful shutdown
    int bound_port() const;  // actual port (useful when port=0)

private:
    void setup_routes();
    void set_cors_headers(httplib::Response& res);
    json error_response(const std::string& code, const std::string& message);

    // Session endpoints
    void handle_create_session(const httplib::Request& req, httplib::Response& res);
    void handle_list_sessions(const httplib::Request& req, httplib::Response& res);
    void handle_delete_session(const httplib::Request& req, httplib::Response& res);

    // Chat endpoints (implemented in Task 4)
    void handle_chat(const httplib::Request& req, httplib::Response& res);
    void handle_stream_chat(const httplib::Request& req, httplib::Response& res);
    void handle_interrupt(const httplib::Request& req, httplib::Response& res);
    void handle_history(const httplib::Request& req, httplib::Response& res);

    // Approval endpoints (implemented in Task 5)
    void handle_list_approvals(const httplib::Request& req, httplib::Response& res);
    void handle_resolve_approval(const httplib::Request& req, httplib::Response& res);
    void handle_approval_stream(const httplib::Request& req, httplib::Response& res);

    // System endpoints
    void handle_health(const httplib::Request& req, httplib::Response& res);
    void handle_models(const httplib::Request& req, httplib::Response& res);

    ServerConfig config_;
    std::unique_ptr<httplib::Server> server_;
    SessionManager sessions_;
    IProvider* provider_;
    tool::ToolRegistry* registry_;
    security::SecurityPolicy* policy_;
    IMemory* shared_memory_;
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace ea::server
```

- [ ] **Step 5: Implement `src/server/HttpServer.cpp` with session + system endpoints**

```cpp
// src/server/HttpServer.cpp
#include "HttpServer.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <chrono>

namespace ea::server {

HttpServer::HttpServer(ServerConfig config,
                       IProvider* provider,
                       tool::ToolRegistry* registry,
                       security::SecurityPolicy* policy,
                       IMemory* shared_memory)
    : config_(std::move(config))
    , server_(std::make_unique<httplib::Server>())
    , sessions_(config_)
    , provider_(provider)
    , registry_(registry)
    , policy_(policy)
    , shared_memory_(shared_memory)
    , start_time_(std::chrono::steady_clock::now()) {
    setup_routes();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::set_cors_headers(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", config_.cors_origin);
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

json HttpServer::error_response(const std::string& code, const std::string& message) {
    return json{{"error", {{"code", code}, {"message", message}}}};
}

void HttpServer::setup_routes() {
    // CORS preflight for all /api/ routes
    server_->Options("/api/.*", [&](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        res.status = 204;
    });

    // System
    server_->Get("/api/health", [&](const httplib::Request& req, httplib::Response& res) {
        handle_health(req, res);
    });
    server_->Get("/api/models", [&](const httplib::Request& req, httplib::Response& res) {
        handle_models(req, res);
    });

    // Sessions
    server_->Post("/api/sessions", [&](const httplib::Request& req, httplib::Response& res) {
        handle_create_session(req, res);
    });
    server_->Get("/api/sessions", [&](const httplib::Request& req, httplib::Response& res) {
        handle_list_sessions(req, res);
    });
    server_->Delete(R"(/api/sessions/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        handle_delete_session(req, res);
    });

    // Chat
    server_->Post(R"(/api/sessions/([^/]+)/chat)", [&](const httplib::Request& req, httplib::Response& res) {
        handle_chat(req, res);
    });
    server_->Get(R"(/api/sessions/([^/]+)/stream)", [&](const httplib::Request& req, httplib::Response& res) {
        handle_stream_chat(req, res);
    });
    server_->Post(R"(/api/sessions/([^/]+)/interrupt)", [&](const httplib::Request& req, httplib::Response& res) {
        handle_interrupt(req, res);
    });
    server_->Get(R"(/api/sessions/([^/]+)/history)", [&](const httplib::Request& req, httplib::Response& res) {
        handle_history(req, res);
    });

    // Approvals
    server_->Get("/api/approvals", [&](const httplib::Request& req, httplib::Response& res) {
        handle_list_approvals(req, res);
    });
    server_->Post(R"(/api/approvals/([^/]+)/resolve)", [&](const httplib::Request& req, httplib::Response& res) {
        handle_resolve_approval(req, res);
    });
    server_->Get("/api/approvals/stream", [&](const httplib::Request& req, httplib::Response& res) {
        handle_approval_stream(req, res);
    });
}

// --- System endpoints ---

void HttpServer::handle_health(const httplib::Request&, httplib::Response& res) {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    auto sessions = sessions_.list();

    json body;
    body["status"] = "ok";
    body["uptime"] = uptime;
    body["sessions"] = sessions.size();

    set_cors_headers(res);
    res.set_content(body.dump(), "application/json");
}

void HttpServer::handle_models(const httplib::Request&, httplib::Response& res) {
    auto models = provider_->list_models();
    json body = models;

    set_cors_headers(res);
    res.set_content(body.dump(), "application/json");
}

// --- Session endpoints ---

void HttpServer::handle_create_session(const httplib::Request& req, httplib::Response& res) {
    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
        return;
    }

    std::string model = body.value("model", "");
    std::string system_prompt = body.value("system_prompt", "");

    AgentLoop::Config loop_cfg;
    auto* session = sessions_.create(provider_, registry_, shared_memory_, policy_, loop_cfg, model, system_prompt);

    if (!session) {
        set_cors_headers(res);
        res.status = 429;
        res.set_content(error_response("max_sessions", "Maximum number of sessions reached").dump(), "application/json");
        return;
    }

    json resp;
    resp["id"] = session->id;
    resp["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        session->last_active.time_since_epoch()).count();

    set_cors_headers(res);
    res.set_content(resp.dump(), "application/json");
}

void HttpServer::handle_list_sessions(const httplib::Request&, httplib::Response& res) {
    auto sessions = sessions_.list();
    json arr = json::array();

    for (auto* s : sessions) {
        json obj;
        obj["id"] = s->id;
        obj["running"] = s->running.load();
        obj["last_active"] = std::chrono::duration_cast<std::chrono::seconds>(
            s->last_active.time_since_epoch()).count();
        if (!s->model.empty()) {
            obj["model"] = s->model;
        }
        arr.push_back(obj);
    }

    set_cors_headers(res);
    res.set_content(arr.dump(), "application/json");
}

void HttpServer::handle_delete_session(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    if (!sessions_.remove(id)) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }

    set_cors_headers(res);
    res.set_content(json{{"ok", true}}.dump(), "application/json");
}

// --- Chat endpoints (stubs for Task 4) ---

void HttpServer::handle_chat(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }
    // Full implementation in Task 4
    set_cors_headers(res);
    res.set_content(error_response("not_implemented", "Chat endpoint not yet implemented").dump(), "application/json");
}

void HttpServer::handle_stream_chat(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }
    // Full implementation in Task 4
    set_cors_headers(res);
    res.set_content(error_response("not_implemented", "Stream endpoint not yet implemented").dump(), "application/json");
}

void HttpServer::handle_interrupt(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }
    // Full implementation in Task 4
    set_cors_headers(res);
    res.set_content(json{{"ok", true}}.dump(), "application/json");
}

void HttpServer::handle_history(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }
    // Full implementation in Task 4
    json body;
    body["messages"] = json::array();
    set_cors_headers(res);
    res.set_content(body.dump(), "application/json");
}

// --- Approval endpoints (stubs for Task 5) ---

void HttpServer::handle_list_approvals(const httplib::Request&, httplib::Response& res) {
    set_cors_headers(res);
    res.set_content(json::array().dump(), "application/json");
}

void HttpServer::handle_resolve_approval(const httplib::Request& req, httplib::Response& res) {
    // Full implementation in Task 5
    set_cors_headers(res);
    res.set_content(json{{"ok", true}}.dump(), "application/json");
}

void HttpServer::handle_approval_stream(const httplib::Request&, httplib::Response& res) {
    // Full implementation in Task 5
    set_cors_headers(res);
    res.set_content(error_response("not_implemented", "Approval stream not yet implemented").dump(), "application/json");
}

// --- Server lifecycle ---

void HttpServer::start() {
    EA_INFO("Server starting on {}:{}", config_.host, config_.port);
    if (!server_->listen(config_.host, config_.port)) {
        EA_ERROR("Server failed to listen on {}:{}", config_.host, config_.port);
    }
}

void HttpServer::stop() {
    server_->stop();
}

int HttpServer::bound_port() const {
    return server_->get_port();
}

}  // namespace ea::server
```

- [ ] **Step 6: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[http]"`
Expected: All 7 HTTP tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/server/HttpServer.h src/server/HttpServer.cpp tests/test_http_server.cpp tests/CMakeLists.txt
git commit -m "feat: implement HttpServer with session CRUD, health, models, CORS"
```

---

### Task 4: Chat + Stream + Interrupt + History Endpoints

**Files:**
- Modify: `src/server/HttpServer.cpp` (replace chat/stream/interrupt/history stubs)
- Modify: `tests/test_http_server.cpp` (add chat/stream/interrupt/history tests)

**Interfaces:**
- Consumes: `ea::server::Session`, `ea::agent::AgentLoop`, `ea::StreamChunk`, `httplib::DataSink` (for SSE)
- Produces: Working chat, stream, interrupt, history endpoints

- [ ] **Step 1: Add chat/interrupt/history tests to `tests/test_http_server.cpp`**

Add these test cases after the existing ones:

```cpp
TEST_CASE("Chat endpoint returns response", "[server][http][chat]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    // Create session first
    json create_req;
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    // Send chat message
    json chat_req;
    chat_req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/" + id + "/chat", chat_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("content"));
}

TEST_CASE("Chat on nonexistent session returns 404", "[server][http][chat]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/nonexistent/chat", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}

TEST_CASE("Interrupt endpoint returns ok", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req;
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Post("/api/sessions/" + id + "/interrupt");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body["ok"] == true);
}

TEST_CASE("History endpoint returns messages array", "[server][http]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req;
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    auto res = cli.Get("/api/sessions/" + id + "/history");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.contains("messages"));
    REQUIRE(body["messages"].is_array());
}

TEST_CASE("Chat with running session returns 409", "[server][http][chat]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json create_req;
    auto create_res = cli.Post("/api/sessions", create_req.dump(), "application/json");
    auto create_body = json::parse(create_res->body);
    std::string id = create_body["id"];

    // Manually set running flag to simulate active session
    auto* session = fx.server->sessions_.get(id);
    session->running.store(true);

    json chat_req;
    chat_req["message"] = "Hello";

    auto res = cli.Post("/api/sessions/" + id + "/chat", chat_req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 409);
}
```

Note: The `sessions_` member needs to be accessible from tests. Add `friend class` or make it public for testing. The simplest approach: make `sessions_` public in HttpServer, or add a getter. Use a test-only getter:

In `HttpServer.h`, add to the public section:
```cpp
    // Test access
    SessionManager& test_sessions() { return sessions_; }
```

Then in the test, use `fx.server->test_sessions().get(id)` instead of `fx.server->sessions_.get(id)`.

- [ ] **Step 2: Implement chat, interrupt, history in `src/server/HttpServer.cpp`**

Replace the stub implementations:

```cpp
void HttpServer::handle_chat(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }

    // Check if already running
    bool expected = false;
    if (!session->running.compare_exchange_strong(expected, true)) {
        set_cors_headers(res);
        res.status = 409;
        res.set_content(error_response("session_busy", "Session " + id + " is already running").dump(), "application/json");
        return;
    }

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        session->running.store(false);
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
        return;
    }

    std::string message = body.value("message", "");
    if (message.empty()) {
        session->running.store(false);
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("missing_message", "Message field is required").dump(), "application/json");
        return;
    }

    // Run agent loop synchronously
    std::string output;
    auto old_output_fn = session->loop->output_fn();
    // We need to capture output — AgentLoop doesn't expose a setter for output_fn,
    // so we use the history after run() completes

    auto result = session->loop->run(message);
    session->running.store(false);

    if (!result.ok()) {
        set_cors_headers(res);
        res.status = 500;
        res.set_content(error_response("agent_error", result.error().message).dump(), "application/json");
        return;
    }

    // Get the last assistant message from history
    auto& history = session->loop->history();
    std::string content;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->role == Role::Assistant && !it->content.empty()) {
            content = it->content;
            break;
        }
    }

    json resp;
    resp["content"] = content;
    resp["stop_reason"] = "stop";

    set_cors_headers(res);
    res.set_content(resp.dump(), "application/json");
}

void HttpServer::handle_stream_chat(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }

    bool expected = false;
    if (!session->running.compare_exchange_strong(expected, true)) {
        set_cors_headers(res);
        res.status = 409;
        res.set_content(error_response("session_busy", "Session " + id + " is already running").dump(), "application/json");
        return;
    }

    std::string message = req.get_param_value("message");
    if (message.empty()) {
        session->running.store(false);
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("missing_message", "Message query parameter is required").dump(), "application/json");
        return;
    }

    // SSE streaming
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, session, message](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            // Set stream function on the loop
            auto stream_fn = [&sink](const StreamChunk& chunk) {
                json event;
                switch (chunk.type) {
                case StreamChunk::Type::Content:
                    event["type"] = "content";
                    event["data"] = chunk.data;
                    break;
                case StreamChunk::Type::ToolCallEnd:
                    if (chunk.tool_call.has_value()) {
                        event["type"] = "tool_call";
                        event["name"] = chunk.tool_call->name;
                        event["arguments"] = chunk.tool_call->arguments;
                    }
                    break;
                case StreamChunk::Type::Done:
                    event["type"] = "done";
                    if (chunk.usage.has_value()) {
                        event["usage"]["input_tokens"] = chunk.usage->input_tokens;
                        event["usage"]["output_tokens"] = chunk.usage->output_tokens;
                    }
                    break;
                case StreamChunk::Type::Error:
                    event["type"] = "error";
                    event["message"] = chunk.data;
                    break;
                default:
                    break;
                }

                if (!event.empty()) {
                    std::string sse = "data: " + event.dump() + "\n\n";
                    sink.write(sse.c_str(), sse.size());
                }
            };

            // Run the loop — this blocks until done
            auto result = session->loop->run(message);
            session->running.store(false);

            if (!result.ok()) {
                json err_event;
                err_event["type"] = "error";
                err_event["message"] = result.error().message;
                std::string sse = "data: " + err_event.dump() + "\n\n";
                sink.write(sse.c_str(), sse.size());
            }

            sink.done();
            return true;
        },
        [session](bool /*success*/) {
            session->running.store(false);
        }
    );

    set_cors_headers(res);
}

void HttpServer::handle_interrupt(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }

    session->loop->interrupt();

    set_cors_headers(res);
    res.set_content(json{{"ok", true}}.dump(), "application/json");
}

void HttpServer::handle_history(const httplib::Request& req, httplib::Response& res) {
    std::string id = req.matches[1];
    auto* session = sessions_.get(id);
    if (!session) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("session_not_found", "Session " + id + " not found").dump(), "application/json");
        return;
    }

    auto& history = session->loop->history();
    json msgs = json::array();

    for (const auto& msg : history) {
        json m;
        switch (msg.role) {
        case Role::System:    m["role"] = "system"; break;
        case Role::User:      m["role"] = "user"; break;
        case Role::Assistant: m["role"] = "assistant"; break;
        case Role::Tool:      m["role"] = "tool"; break;
        }
        m["content"] = msg.content;
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
        if (msg.name.has_value()) {
            m["name"] = msg.name.value();
        }
        msgs.push_back(m);
    }

    json body;
    body["messages"] = msgs;

    set_cors_headers(res);
    res.set_content(body.dump(), "application/json");
}
```

- [ ] **Step 3: Add `test_sessions()` getter and `output_fn()` to AgentLoop**

In `src/server/HttpServer.h`, add to public section:
```cpp
    SessionManager& test_sessions() { return sessions_; }
```

In `src/agent/AgentLoop.h`, the `output_fn_` is private. We don't need to modify it for the chat handler — we read from `history()` after `run()` completes. No changes needed to AgentLoop.

- [ ] **Step 4: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[http]"`
Expected: All HTTP tests PASS (including new chat/interrupt/history tests)

- [ ] **Step 5: Commit**

```bash
git add src/server/HttpServer.h src/server/HttpServer.cpp tests/test_http_server.cpp
git commit -m "feat: implement chat, stream, interrupt, history endpoints"
```

---

### Task 5: Approval Endpoints

**Files:**
- Modify: `src/server/HttpServer.cpp` (replace approval stubs (replace approval stubs)
- Modify: `src/server/HttpServer.h` (add approval notification support)
- Modify: `tests/test_http_server.cpp` (add approval tests)

**Interfaces:**
- Consumes: `ea::security::PendingApprovalHandler`, `ea::security::PendingApproval`, `ea::security::ApprovalDecision`
- Produces: Working approval list, resolve, and SSE stream endpoints

- [ ] **Step 1: Add approval tests to `tests/test_http_server.cpp`**

```cpp
TEST_CASE("Approvals list returns array", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    auto res = cli.Get("/api/approvals");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 200);

    auto body = json::parse(res->body);
    REQUIRE(body.is_array());
}

TEST_CASE("Resolve nonexistent approval returns 404", "[server][http][approval]") {
    ServerFixture fx;
    httplib::Client cli(fx.base_url());

    json req;
    req["decision"] = "approved";

    auto res = cli.Post("/api/approvals/999/resolve", req.dump(), "application/json");
    REQUIRE(res != nullptr);
    REQUIRE(res->status == 404);
}
```

- [ ] **Step 2: Implement approval endpoints in `src/server/HttpServer.cpp`**

Replace the stub implementations:

```cpp
void HttpServer::handle_list_approvals(const httplib::Request&, httplib::Response& res) {
    // Collect pending approvals from all sessions
    json arr = json::array();
    auto sessions = sessions_.list();
    for (auto* session : sessions) {
        if (!session->approval) continue;
        auto pending = session->approval->pending_list();
        for (auto* p : pending) {
            json obj;
            obj["id"] = p->id;
            obj["session_id"] = session->id;
            obj["tool"] = p->request.tool_name;
            obj["arguments"] = p->request.arguments;
            obj["description"] = p->request.description;
            arr.push_back(obj);
        }
    }

    set_cors_headers(res);
    res.set_content(arr.dump(), "application/json");
}

void HttpServer::handle_resolve_approval(const httplib::Request& req, httplib::Response& res) {
    std::string approval_id = req.matches[1];

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("invalid_json", "Failed to parse request body").dump(), "application/json");
        return;
    }

    std::string decision_str = body.value("decision", "");
    security::ApprovalDecision decision;
    if (decision_str == "approved") {
        decision = security::ApprovalDecision::Approved;
    } else if (decision_str == "rejected") {
        decision = security::ApprovalDecision::Rejected;
    } else if (decision_str == "aborted") {
        decision = security::ApprovalDecision::Aborted;
    } else {
        set_cors_headers(res);
        res.status = 400;
        res.set_content(error_response("invalid_decision", "Decision must be 'approved', 'rejected', or 'aborted'").dump(), "application/json");
        return;
    }

    // Find the approval across all sessions
    bool found = false;
    auto sessions = sessions_.list();
    for (auto* session : sessions) {
        if (!session->approval) continue;
        if (session->approval->resolve(approval_id, decision)) {
            found = true;
            break;
        }
    }

    if (!found) {
        set_cors_headers(res);
        res.status = 404;
        res.set_content(error_response("approval_not_found", "Approval " + approval_id + " not found").dump(), "application/json");
        return;
    }

    set_cors_headers(res);
    res.set_content(json{{"ok", true}}.dump(), "application/json");
}

void HttpServer::handle_approval_stream(const httplib::Request&, httplib::Response& res) {
    // SSE stream for approval notifications
    // Polls pending approvals periodically and sends new ones
    res.set_chunked_content_provider(
        "text/event-stream",
        [this](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            // Send initial comment to establish connection
            std::string comment = ": connected\n\n";
            sink.write(comment.c_str(), comment.size());

            // Simple polling loop — check for new approvals every 2 seconds
            // In production, this would use a condition variable or event system
            std::set<std::string> sent_ids;
            for (int i = 0; i < 150; ++i) {  // 5 minutes max (150 * 2s)
                auto sessions = sessions_.list();
                for (auto* session : sessions) {
                    if (!session->approval) continue;
                    auto pending = session->approval->pending_list();
                    for (auto* p : pending) {
                        if (sent_ids.count(p->id)) continue;
                        sent_ids.insert(p->id);

                        json event;
                        event["type"] = "approval_request";
                        event["id"] = p->id;
                        event["session_id"] = session->id;
                        event["tool"] = p->request.tool_name;
                        event["arguments"] = p->request.arguments;
                        event["description"] = p->request.description;

                        std::string sse = "data: " + event.dump() + "\n\n";
                        sink.write(sse.c_str(), sse.size());
                    }
                }

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            sink.done();
            return true;
        }
    );

    set_cors_headers(res);
}
```

Add the missing include at the top of HttpServer.cpp:
```cpp
#include <set>
#include <thread>
```

- [ ] **Step 3: Build and run tests**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests "[approval]"`
Expected: All approval tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/server/HttpServer.h src/server/HttpServer.cpp tests/test_http_server.cpp
git commit -m "feat: implement approval list, resolve, and SSE stream endpoints"
```

---

### Task 6: main.cpp Integration + Config Loading

**Files:**
- Modify: `src/main.cpp` (add EA_MODE_SERVER branch)
- Modify: `src/config/Config.h` (add server config fields if not already done in Task 1)

**Interfaces:**
- Consumes: `ea::server::HttpServer`, `ea::server::ServerConfig`, `ea::config::AppConfig`
- Produces: Server mode entry point in main.cpp

- [ ] **Step 1: Add server mode branch to `src/main.cpp`**

After the existing `#ifdef EA_MODE_CLI` / `#elif defined(EA_MODE_SERVER)` approval handler block (around line 94-100), and replacing the CLI interactive loop (lines 209-224), add the server mode branch:

Replace the interactive loop section with:

```cpp
    // 9. Run mode
#ifdef EA_MODE_SERVER
    // Server mode: start HTTP API
    ea::server::ServerConfig srv_cfg;
    srv_cfg.host = cfg.server.host;
    srv_cfg.port = cfg.server.port;
    srv_cfg.max_sessions = cfg.server.max_sessions;
    srv_cfg.cors_origin = cfg.server.cors_origin;
    srv_cfg.session_idle_timeout = cfg.server.session_idle_timeout;

    auto http_server = std::make_unique<ea::server::HttpServer>(
        srv_cfg, provider.get(), &registry, security.get(), memory.get()
    );

    EA_INFO("Server starting on {}:{}", srv_cfg.host, srv_cfg.port);
    http_server->start();
#else
    // CLI mode: interactive loop
    std::string input;
    std::cout << "embedded-agent v0.1.0 (type /quit to exit)" << std::endl;

    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit" || input == "/exit") break;
        if (input.empty()) continue;

        auto result = loop.run(input);
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }
#endif
```

Also add the include at the top of main.cpp:
```cpp
#include "server/HttpServer.h"
```

- [ ] **Step 2: Build and verify**

Run: `cd build && cmake --build . -j$(nproc)`
Expected: Clean build

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add EA_MODE_SERVER entry point in main.cpp"
```

---

### Task 7: Final Verification

**Files:** None (verification only)

- [ ] **Step 1: Run full test suite**

Run: `cd build && cmake --build . -j$(nproc) && ./tests/ea-tests`
Expected: All tests pass (previous tests + new server tests)

- [ ] **Step 2: Run server-specific tests**

Run: `cd build && ./tests/ea-tests "[server]"`
Expected: All server tests pass

- [ ] **Step 3: Verify no regressions**

Run: `cd build && ./tests/ea-tests --reporter console`
Expected: 0 failures, 0 errors

- [ ] **Step 4: Push all commits**

```bash
git push origin develop
```
