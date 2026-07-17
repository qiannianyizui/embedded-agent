# Phase 3C: MCP Client Design

**Date:** 2026-07-17
**Status:** Approved
**Scope:** MCP (Model Context Protocol) client for connecting to external tool servers

## Overview

Add an MCP client that connects to external MCP servers, discovers their tools, and integrates them into the existing `ToolRegistry` as `Toolset`s. Only the **Tools** capability is implemented (not Resources or Prompts). Communication uses a pluggable `ITransport` interface with `StdioTransport` as the first implementation (child process stdin/stdout). MCP tools are wrapped as `ITool` via `McpToolAdapter`, so the existing approval system and security policy work automatically.

**Design approach:** Three-layer architecture — Transport (pluggable I/O) → Protocol (McpClient, JSON-RPC 2.0) → Adapter (McpToolAdapter → ITool → Toolset). Each MCP server becomes a named Toolset in the ToolRegistry.

---

## 1. Transport Layer

### ITransport Interface

```cpp
// src/mcp/ITransport.h
#pragma once
#include "common/base/Result.h"
#include "nlohmann/json.hpp"
#include <string>

namespace ea::mcp {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual Result<void> start() = 0;
    virtual Result<void> stop() = 0;

    // Send a JSON-RPC request and wait for the response
    virtual Result<nlohmann::json> send(const nlohmann::json& request) = 0;

    virtual bool is_running() const = 0;
};

}  // namespace ea::mcp
```

### StdioTransport

Communicates with a child process via stdin/stdout. Each JSON-RPC message is one line (newline-delimited).

```cpp
// src/mcp/StdioTransport.h
#pragma once
#include "ITransport.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

namespace ea::mcp {

class StdioTransport : public ITransport {
public:
    struct Config {
        std::string command;                          // e.g. "npx"
        std::vector<std::string> args;                // e.g. {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"}
        std::map<std::string, std::string> env;       // Additional environment variables
    };

    explicit StdioTransport(Config config);

    Result<void> start() override;
    Result<void> stop() override;
    Result<nlohmann::json> send(const nlohmann::json& request) override;
    bool is_running() const override;

private:
    Config config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;  // pimpl for process handles
};

}  // namespace ea::mcp
```

**Implementation notes:**
- `start()` launches the child process with `pipe2()` for stdin/stdout
- `send()` writes JSON + `\n` to child's stdin, reads one line from stdout
- `stop()` sends SIGTERM, waits briefly, then SIGKILL if needed
- Uses `select()` or `poll()` for read timeout (default 30s)
- `impl_` uses pimpl pattern to hide `<unistd.h>` / process headers from the header

---

## 2. Protocol Layer — McpClient

Implements MCP protocol over JSON-RPC 2.0.

```cpp
// src/mcp/McpClient.h
#pragma once
#include "ITransport.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::mcp {

class McpClient {
public:
    explicit McpClient(std::unique_ptr<ITransport> transport);

    // Lifecycle
    Result<void> connect();     // initialize handshake
    Result<void> disconnect();  // send shutdown notification

    // Tool operations
    Result<std::vector<ToolSpec>> list_tools();
    Result<ToolResult> call_tool(const std::string& name, const nlohmann::json& arguments);

    bool is_connected() const;

private:
    Result<nlohmann::json> send_request(const std::string& method,
                                         const nlohmann::json& params = nullptr);

    std::unique_ptr<ITransport> transport_;
    bool connected_ = false;
    int next_id_ = 1;
};

}  // namespace ea::mcp
```

### MCP Protocol Flow

```
1. connect():
   → send_request("initialize", {protocolVersion: "2024-11-05", capabilities: {}, clientInfo: {name: "embedded-agent"}})
   ← receive: {protocolVersion, capabilities, serverInfo}
   → send_request("notifications/initialized")  (notification, no response)

2. list_tools():
   → send_request("tools/list")
   ← receive: {tools: [{name, description, inputSchema}, ...]}

3. call_tool(name, args):
   → send_request("tools/call", {name: "...", arguments: {...}})
   ← receive: {content: [{type: "text", text: "..."}], isError: false}

4. disconnect():
   → send notification (no response expected)
   → transport_->stop()
```

### JSON-RPC 2.0 Message Format

```json
// Request
{"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}}

// Response
{"jsonrpc": "2.0", "id": 1, "result": {"tools": [...]}}

// Error
{"jsonrpc": "2.0", "id": 1, "error": {"code": -32600, "message": "Invalid request"}}
```

---

## 3. Adapter Layer — McpToolAdapter

Wraps each MCP tool as an `ITool` so it integrates with the existing tool system.

```cpp
// src/mcp/McpToolAdapter.h
#pragma once
#include "core/ITool.h"
#include "McpClient.h"
#include <memory>

namespace ea::mcp {

class McpToolAdapter : public ITool {
public:
    McpToolAdapter(std::shared_ptr<McpClient> client, ToolSpec spec, bool dangerous = false);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parameters_schema() const override;
    Result<ToolResult> execute(const nlohmann::json& args) override;
    bool is_mutating() const override;
    bool is_dangerous() const override;

private:
    std::shared_ptr<McpClient> client_;
    ToolSpec spec_;
    bool dangerous_;
};

}  // namespace ea::mcp
```

**Implementation notes:**
- `execute()` calls `client_->call_tool(spec_.name, args)` and maps the result
- `is_mutating()` returns `true` by default (MCP doesn't distinguish)
- `is_dangerous()` returns the `dangerous_` flag from config
- `client_` is `shared_ptr` because multiple tools from the same server share one client

---

## 4. Configuration

### TOML Config

```toml
[[mcp.servers]]
name = "filesystem"
command = "npx"
args = ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
dangerous = false

[[mcp.servers]]
name = "github"
command = "mcp-server-github"
env = { GITHUB_TOKEN = "ghp_xxx" }
dangerous = true
```

### Config Struct

```cpp
struct McpServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool dangerous = false;
};

struct AppConfig {
    ProviderConfig provider;
    MemoryConfig memory;
    SecurityConfig security;
    AgentConfig agent;
    std::vector<McpServerConfig> mcp_servers;  // NEW
    std::string config_path;
};
```

### Config Parsing

```cpp
// In Config.cpp, after existing sections:
if (data.contains("mcp") && toml::find(data, "mcp").contains("servers")) {
    auto servers = toml::find<std::vector<toml::table>>(toml::find(data, "mcp"), "servers");
    for (const auto& server : servers) {
        McpServerConfig sc;
        sc.name = toml::find<std::string>(server, "name");
        sc.command = toml::find<std::string>(server, "command");
        if (server.contains("args")) {
            sc.args = toml::find<std::vector<std::string>>(server, "args");
        }
        if (server.contains("env")) {
            auto env_table = toml::find<toml::table>(server, "env");
            for (const auto& [k, v] : env_table) {
                sc.env[k] = toml::find<std::string>(server, "env", k);
            }
        }
        sc.dangerous = toml::find_or<bool>(server, "dangerous", false);
        cfg.mcp_servers.push_back(std::move(sc));
    }
}
```

---

## 5. main.cpp Integration

```cpp
// After tool registration, before AgentLoop creation:

// 10. Connect MCP servers and register their tools
std::vector<std::shared_ptr<ea::mcp::McpClient>> mcp_clients;

for (auto& server_cfg : cfg.mcp_servers) {
    EA_INFO("Connecting MCP server: {}", server_cfg.name);

    ea::mcp::StdioTransport::Config transport_cfg;
    transport_cfg.command = server_cfg.command;
    transport_cfg.args = server_cfg.args;
    transport_cfg.env = server_cfg.env;

    auto transport = std::make_unique<ea::mcp::StdioTransport>(std::move(transport_cfg));
    auto client = std::make_shared<ea::mcp::McpClient>(std::move(transport));

    auto conn_result = client->connect();
    if (!conn_result.ok()) {
        EA_ERROR("MCP server '{}' connection failed: {}", server_cfg.name, conn_result.error().message);
        continue;
    }

    auto tools_result = client->list_tools();
    if (!tools_result.ok()) {
        EA_ERROR("MCP server '{}' list_tools failed: {}", server_cfg.name, tools_result.error().message);
        continue;
    }

    auto toolset = std::make_unique<ea::tool::Toolset>(server_cfg.name);
    for (auto& spec : tools_result.value()) {
        toolset->add(std::make_unique<ea::mcp::McpToolAdapter>(client, spec, server_cfg.dangerous));
    }
    registry.register_toolset(std::move(toolset));
    mcp_clients.push_back(client);

    EA_INFO("MCP server '{}' connected with {} tools", server_cfg.name, tools_result.value().size());
}

// mcp_clients must outlive the AgentLoop — they're kept in main scope
```

---

## 6. Build Config

Add `EA_ENABLE_MCP` compile flag (default ON for CLI/Server, OFF for Embedded):

```cmake
option(EA_ENABLE_MCP "Enable MCP client" ON)

if(EA_MODE_EMBEDDED)
    set(EA_ENABLE_MCP OFF CACHE BOOL "Disable MCP for embedded" FORCE)
endif()
```

In `build_config.h.in`:
```
#cmakedefine EA_ENABLE_MCP
```

---

## 7. Test Strategy

### Unit Tests

| Test File | Coverage |
|-----------|----------|
| `tests/test_mcp_transport.cpp` | StdioTransport with echo subprocess |
| `tests/test_mcp_client.cpp` | McpClient with MockTransport |
| `tests/test_mcp_tool_adapter.cpp` | McpToolAdapter → ITool contract |

### MockTransport (for unit tests)

```cpp
class MockTransport : public ITransport {
public:
    nlohmann::json next_response;
    std::vector<nlohmann::json> sent_requests;

    Result<void> start() override { running_ = true; return {}; }
    Result<void> stop() override { running_ = false; return {}; }
    Result<nlohmann::json> send(const nlohmann::json& request) override {
        sent_requests.push_back(request);
        return next_response;
    }
    bool is_running() const override { return running_; }

private:
    bool running_ = false;
};
```

### Key Test Cases

1. McpClient connect sends initialize request with correct protocol version
2. McpClient list_tools returns ToolSpec vector
3. McpClient call_tool sends tools/call request and maps response to ToolResult
4. McpClient call_tool maps isError=true to ToolResult with is_error=true
5. McpToolAdapter delegates execute to McpClient
6. McpToolAdapter is_dangerous returns config value
7. StdioTransport start/stop with echo command
8. StdioTransport send/receive with echo command

---

## 8. File Manifest

### New Files

| File | Responsibility |
|------|---------------|
| `src/mcp/ITransport.h` | Pluggable transport interface |
| `src/mcp/StdioTransport.h` | Stdio transport declaration |
| `src/mcp/StdioTransport.cpp` | Stdio transport implementation (process management) |
| `src/mcp/McpClient.h` | MCP protocol client declaration |
| `src/mcp/McpClient.cpp` | MCP protocol implementation (JSON-RPC 2.0) |
| `src/mcp/McpToolAdapter.h` | ITool adapter for MCP tools (header-only) |
| `src/mcp/CMakeLists.txt` | MCP module build |
| `tests/test_mcp_transport.cpp` | Transport tests |
| `tests/test_mcp_client.cpp` | Client protocol tests |
| `tests/test_mcp_tool_adapter.cpp` | Adapter tests |

### Modified Files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `add_subdirectory(src/mcp)`, EA_ENABLE_MCP option |
| `include/ea/build_config.h.in` | Add `#cmakedefine EA_ENABLE_MCP` |
| `src/config/Config.h` | Add McpServerConfig struct and mcp_servers to AppConfig |
| `src/config/Config.cpp` | Parse [[mcp.servers]] TOML array |
| `src/main.cpp` | Connect MCP servers, register as Toolsets |
| `tests/CMakeLists.txt` | Add 3 new test files, link ea-mcp |

---

## Scope Summary

| Dimension | Content |
|-----------|---------|
| Goal | MCP client for connecting to external tool servers |
| Capability | Tools only (not Resources/Prompts) |
| Transport | Pluggable ITransport, StdioTransport first |
| Protocol | JSON-RPC 2.0 over MCP spec 2024-11-05 |
| Integration | Each MCP server → named Toolset in ToolRegistry |
| Approval | dangerous flag in config → is_dangerous() → auto-approval |
| Config | [[mcp.servers]] TOML array with name/command/args/env/dangerous |
| Build | EA_ENABLE_MCP compile flag (default ON, OFF for Embedded) |

**Not in scope:** MCP Resources, MCP Prompts, MCP Sampling, HTTP+SSE transport (future), MCP server implementation.
