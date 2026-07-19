# Phase 4B: Server Mode (HTTP API) — Design Spec

**Date:** 2026-07-19
**Status:** Draft

## Overview

Add an HTTP server mode to embedded-agent, exposing the agent loop as a RESTful API with SSE streaming. The server manages multiple independent sessions, each with its own AgentLoop, history, and memory scope. Approval requests from the security system are surfaced via SSE and resolved via REST endpoints.

## Architecture

```
main.cpp (EA_MODE_SERVER)
  └── HttpServer (httplib::Server wrapper)
        ├── SessionManager (session pool)
        │     └── Session[] (each: AgentLoop + history + ScopedMemory)
        └── Routes:
              POST   /api/sessions              → create session
              GET    /api/sessions              → list sessions
              DELETE /api/sessions/:id          → delete session
              POST   /api/sessions/:id/chat     → send message (sync)
              GET    /api/sessions/:id/stream   → SSE streaming chat
              POST   /api/sessions/:id/interrupt → interrupt running loop
              GET    /api/sessions/:id/history  → get message history
              GET    /api/approvals             → list pending approvals
              POST   /api/approvals/:id/resolve → resolve approval
              GET    /api/approvals/stream      → SSE approval notifications
              GET    /api/health                → health check
              GET    /api/models                → list available models
```

## Components

### ServerConfig

```cpp
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int max_sessions = 100;
    std::string cors_origin = "*";
    std::chrono::seconds session_idle_timeout{3600};
};
```

### Session

```cpp
struct Session {
    std::string id;                                    // UUID
    std::shared_ptr<AgentLoop> loop;                   // independent loop
    std::vector<Message> history;                       // message history
    std::atomic<bool> running{false};                   // currently executing
    std::chrono::steady_clock::time_point last_active;  // last activity
};
```

Each session initializes an independent `AgentLoop`. Sessions share the same `IProvider`, `ToolRegistry`, and `SecurityPolicy`, but each has its own `ScopedMemory` (isolated by session ID) and `PendingApprovalHandler`.

### SessionManager

```cpp
class SessionManager {
public:
    Session* create(const AgentLoop::Config& loop_cfg, IProvider*, ToolRegistry*, IMemory*, SecurityPolicy*);
    Session* get(const std::string& id);
    bool remove(const std::string& id);
    std::vector<Session*> list();
    void cleanup_idle();
private:
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    ServerConfig config_;
};
```

Thread-safe: all operations guarded by mutex. `cleanup_idle()` called periodically to remove sessions past `session_idle_timeout`.

### HttpServer

```cpp
class HttpServer {
public:
    HttpServer(ServerConfig config, IProvider*, ToolRegistry*, SecurityPolicy*);
    void start();   // blocking
    void stop();    // graceful shutdown
private:
    void setup_routes();
    // handlers...
    ServerConfig config_;
    httplib::Server server_;
    SessionManager sessions_;
    IProvider* provider_;
    ToolRegistry* registry_;
    SecurityPolicy* policy_;
};
```

## API Endpoints

### Session Management

| Method | Path | Request Body | Response |
|--------|------|-------------|----------|
| POST | `/api/sessions` | `{"model":"gpt-4o","system_prompt":"..."}` | `{"id":"sess_abc","created_at":"..."}` |
| GET | `/api/sessions` | — | `[{"id":"sess_abc","running":false,"last_active":"..."}]` |
| DELETE | `/api/sessions/:id` | — | `{"ok":true}` |

### Chat

| Method | Path | Request Body | Response |
|--------|------|-------------|----------|
| POST | `/api/sessions/:id/chat` | `{"message":"Hello","stream":false}` | `{"content":"...","tool_calls":[],"usage":{}}` |
| GET | `/api/sessions/:id/stream` | query: `message=Hello` | SSE event stream |
| POST | `/api/sessions/:id/interrupt` | — | `{"ok":true}` |
| GET | `/api/sessions/:id/history` | — | `{"messages":[...]}` |

### Approvals

| Method | Path | Request Body | Response |
|--------|------|-------------|----------|
| GET | `/api/approvals` | — | `[{"id":"apr_1","tool":"shell","arguments":{}}]` |
| POST | `/api/approvals/:id/resolve` | `{"decision":"approved"}` | `{"ok":true}` |
| GET | `/api/approvals/stream` | — | SSE approval notification stream |

### System

| Method | Path | Response |
|--------|------|----------|
| GET | `/api/health` | `{"status":"ok","uptime":123,"sessions":5}` |
| GET | `/api/models` | `["gpt-4o","claude-sonnet-4-6","llama3"]` |

## SSE Event Format

All SSE events use `data: <json>\n\n` format.

### Chat Stream Events

```
data: {"type":"content","data":"Hello"}

data: {"type":"tool_call","name":"read_file","arguments":{...}}

data: {"type":"tool_result","name":"read_file","output":"...","error":false}

data: {"type":"done","usage":{"input_tokens":10,"output_tokens":5}}

data: {"type":"error","message":"..."}
```

StreamChunk type mapping:
- `StreamChunk::Content` → `{"type":"content","data":...}`
- `StreamChunk::ToolCallEnd` → `{"type":"tool_call","name":...,"arguments":...}`
- `StreamChunk::Done` → `{"type":"done","usage":...}`
- Error → `{"type":"error","message":...}`

### Approval Stream Events

```
data: {"type":"approval_request","id":"apr_1","tool":"shell","arguments":{...},"description":"..."}
```

## Error Response Format

All errors use a unified format:

```json
{"error": {"code": "session_not_found", "message": "Session sess_abc not found"}}
```

HTTP status codes:
- 400: Invalid parameters
- 404: Session not found
- 409: Session already running (conflict)
- 429: Max sessions exceeded
- 500: Internal error

## CORS

All responses include `Access-Control-Allow-Origin: <cors_origin>` (default `*`). OPTIONS preflight handled automatically by httplib.

## main.cpp Integration

Under `EA_MODE_SERVER`, replace the CLI interactive loop:

```cpp
#ifdef EA_MODE_SERVER
    ea::server::ServerConfig srv_cfg;
    srv_cfg.host = "0.0.0.0";
    srv_cfg.port = 8080;
    // ... load from config

    auto server = std::make_unique<ea::server::HttpServer>(
        srv_cfg, provider.get(), &registry, security.get()
    );
    EA_INFO("Server starting on {}:{}", srv_cfg.host, srv_cfg.port);
    server->start();
#endif
```

## File Layout

```
src/server/
  HttpServer.h         — HttpServer class declaration
  HttpServer.cpp       — Route setup + handlers
  SessionManager.h     — SessionManager + Session declarations
  SessionManager.cpp   — Session lifecycle management
  ServerConfig.h       — ServerConfig struct
```

New namespace: `ea::server`

## Testing Strategy

- **SessionManager unit tests**: create/find/remove/idle cleanup, max session limit
- **HttpServer integration tests**: use `httplib::Client` to hit endpoints, verify JSON responses
- **SSE tests**: connect to stream endpoint, verify event format and ordering
- **Approval flow tests**: request → SSE notification → resolve → agent continues
- **Test tag**: `[server]`

## Dependencies

- **cpp-httplib**: Already a project dependency (used for HttpClient). Its `httplib::Server` provides the HTTP server. No new external dependencies needed.

## Constraints

- Thread safety: `SessionManager` mutex-protected; each `AgentLoop` runs in its own thread; httplib handles requests in thread pool
- Session isolation: each session has independent memory scope via `ScopedMemory`
- Graceful shutdown: `stop()` drains active sessions before exiting
- No WebSocket: SSE only for streaming, keeping dependency footprint minimal
