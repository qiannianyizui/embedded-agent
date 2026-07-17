# Phase 3C: MCP Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add MCP client that connects to external MCP servers via pluggable transport, discovers tools, and integrates them as Toolsets in the ToolRegistry.

**Architecture:** Three-layer: Transport (ITransport → StdioTransport) → Protocol (McpClient, JSON-RPC 2.0) → Adapter (McpToolAdapter → ITool → Toolset). Each MCP server becomes a named Toolset.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, Catch2, POSIX process APIs

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/mcp/ITransport.h` | Pluggable transport interface |
| `src/mcp/StdioTransport.h` | Stdio transport declaration (pimpl) |
| `src/mcp/StdioTransport.cpp` | Stdio transport implementation (child process) |
| `src/mcp/McpClient.h` | MCP protocol client declaration |
| `src/mcp/McpClient.cpp` | MCP protocol implementation (JSON-RPC 2.0) |
| `src/mcp/McpToolAdapter.h` | ITool adapter for MCP tools (header-only) |
| `src/mcp/CMakeLists.txt` | MCP module build |
| `tests/test_mcp_client.cpp` | Client + MockTransport + adapter tests |

### Modified Files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `add_subdirectory(src/mcp)`, EA_ENABLE_MCP option, link ea-mcp |
| `include/ea/build_config.h.in` | Add `#cmakedefine EA_ENABLE_MCP` |
| `src/config/Config.h` | Add McpServerConfig struct, mcp_servers to AppConfig |
| `src/config/Config.cpp` | Parse [[mcp.servers]] TOML array |
| `src/main.cpp` | Connect MCP servers, register as Toolsets |
| `tests/CMakeLists.txt` | Add test_mcp_client.cpp, link ea-mcp |

---

### Task 1: ITransport Interface

**Files:**
- Create: `src/mcp/ITransport.h`

- [ ] **Step 1: Write ITransport.h**

```cpp
// src/mcp/ITransport.h
#pragma once
#include "common/base/Result.h"
#include "nlohmann/json.hpp"

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

- [ ] **Step 2: Commit**

```bash
git add src/mcp/ITransport.h
git commit -m "feat(mcp): add ITransport interface for pluggable MCP transport"
```

---

### Task 2: StdioTransport

**Files:**
- Create: `src/mcp/StdioTransport.h`
- Create: `src/mcp/StdioTransport.cpp`

- [ ] **Step 1: Write StdioTransport.h**

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
        std::string command;
        std::vector<std::string> args;
        std::map<std::string, std::string> env;
    };

    explicit StdioTransport(Config config);
    ~StdioTransport() override;

    Result<void> start() override;
    Result<void> stop() override;
    Result<nlohmann::json> send(const nlohmann::json& request) override;
    bool is_running() const override;

private:
    Config config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ea::mcp
```

- [ ] **Step 2: Write StdioTransport.cpp**

```cpp
// src/mcp/StdioTransport.cpp
#include "StdioTransport.h"
#include "common/io/Logger.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <thread>

namespace ea::mcp {

// Pimpl for process handles — hides POSIX headers
struct StdioTransport::Impl {
    FILE* child_stdin = nullptr;   // Write to child's stdin
    FILE* child_stdout = nullptr;  // Read from child's stdout
    pid_t child_pid = -1;
    bool running = false;
};

StdioTransport::StdioTransport(Config config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

StdioTransport::~StdioTransport() {
    if (impl_->running) {
        stop();
    }
}

Result<void> StdioTransport::start() {
    if (impl_->running) return {};

    // Build command string
    std::string cmd = config_.command;
    for (const auto& arg : config_.args) {
        cmd += " ";
        // Simple quoting for arguments with spaces
        if (arg.find(' ') != std::string::npos) {
            cmd += "'" + arg + "'";
        } else {
            cmd += arg;
        }
    }

    // Set environment variables
    for (const auto& [key, value] : config_.env) {
        setenv(key.c_str(), value.c_str(), 1);
    }

    // Launch child process with bidirectional pipe
    // Use popen for simplicity — write to stdin, read from stdout
    // For proper bidirectional IPC, we use pipe2 + fork
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return Error::io("Failed to create pipes for MCP server");
    }

    pid_t pid = fork();
    if (pid < 0) {
        return Error::io("Failed to fork for MCP server");
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);   // Close write end
        close(stdout_pipe[0]);  // Close read end
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    close(stdin_pipe[0]);   // Close read end
    close(stdout_pipe[1]);  // Close write end

    impl_->child_pid = pid;
    impl_->child_stdin = fdopen(stdin_pipe[1], "w");
    impl_->child_stdout = fdopen(stdout_pipe[0], "r");
    impl_->running = true;

    if (!impl_->child_stdin || !impl_->child_stdout) {
        stop();
        return Error::io("Failed to open pipes as FILE*");
    }

    // Set line buffering for stdin
    setvbuf(impl_->child_stdin, nullptr, _IOLBF, 0);

    EA_INFO("MCP server started: pid={}, command={}", pid, cmd);
    return {};
}

Result<void> StdioTransport::stop() {
    if (!impl_->running) return {};

    // Close stdin to signal EOF
    if (impl_->child_stdin) {
        fclose(impl_->child_stdin);
        impl_->child_stdin = nullptr;
    }

    // Wait for child to exit (with timeout)
    if (impl_->child_pid > 0) {
        int status;
        kill(impl_->child_pid, SIGTERM);

        // Wait up to 2 seconds
        for (int i = 0; i < 20; ++i) {
            if (waitpid(impl_->child_pid, &status, WNOHANG) != 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Force kill if still running
        kill(impl_->child_pid, SIGKILL);
        waitpid(impl_->child_pid, &status, 0);
    }

    if (impl_->child_stdout) {
        fclose(impl_->child_stdout);
        impl_->child_stdout = nullptr;
    }

    impl_->child_pid = -1;
    impl_->running = false;
    return {};
}

Result<nlohmann::json> StdioTransport::send(const nlohmann::json& request) {
    if (!impl_->running || !impl_->child_stdin || !impl_->child_stdout) {
        return Error::io("MCP transport not running");
    }

    // Write request as single line
    std::string line = request.dump() + "\n";
    fputs(line.c_str(), impl_->child_stdin);
    fflush(impl_->child_stdin);

    // Read response line
    char buffer[65536];
    if (!fgets(buffer, sizeof(buffer), impl_->child_stdout)) {
        return Error::io("MCP server closed connection");
    }

    // Parse JSON response
    try {
        return nlohmann::json::parse(buffer);
    } catch (const nlohmann::json::parse_error& e) {
        return Error::parse(std::string("MCP response parse error: ") + e.what());
    }
}

bool StdioTransport::is_running() const {
    return impl_->running;
}

}  // namespace ea::mcp
```

- [ ] **Step 3: Commit**

```bash
git add src/mcp/StdioTransport.h src/mcp/StdioTransport.cpp
git commit -m "feat(mcp): add StdioTransport with child process pipe management"
```

---

### Task 3: McpClient

**Files:**
- Create: `src/mcp/McpClient.h`
- Create: `src/mcp/McpClient.cpp`

- [ ] **Step 1: Write McpClient.h**

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
    Result<void> disconnect();  // send shutdown + stop transport

    // Tool operations
    Result<std::vector<ToolSpec>> list_tools();
    Result<ToolResult> call_tool(const std::string& name, const nlohmann::json& arguments);

    bool is_connected() const;

private:
    Result<nlohmann::json> send_request(const std::string& method,
                                         const nlohmann::json& params);

    std::unique_ptr<ITransport> transport_;
    bool connected_ = false;
    int next_id_ = 1;
};

}  // namespace ea::mcp
```

- [ ] **Step 2: Write McpClient.cpp**

```cpp
// src/mcp/McpClient.cpp
#include "McpClient.h"
#include "common/io/Logger.h"

namespace ea::mcp {

McpClient::McpClient(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

Result<void> McpClient::connect() {
    if (connected_) return {};

    // Start transport
    auto start_result = transport_->start();
    if (!start_result.ok()) return start_result;

    // Send initialize request
    nlohmann::json params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {
            {"name", "embedded-agent"},
            {"version", "0.1.0"}
        }}
    };

    auto init_result = send_request("initialize", params);
    if (!init_result.ok()) {
        transport_->stop();
        return init_result;
    }

    // Check protocol version
    auto& response = init_result.value();
    if (response.contains("protocolVersion")) {
        std::string server_version = response["protocolVersion"];
        EA_INFO("MCP server protocol version: {}", server_version);
    }

    // Send initialized notification (no response expected)
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    transport_->send(notif);

    connected_ = true;
    EA_INFO("MCP client connected");
    return {};
}

Result<void> McpClient::disconnect() {
    if (!connected_) return {};

    // Send shutdown notification
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
        {"params", nlohmann::json::object()}
    };
    transport_->send(notif);

    transport_->stop();
    connected_ = false;
    return {};
}

Result<nlohmann::json> McpClient::send_request(const std::string& method,
                                                 const nlohmann::json& params) {
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", next_id_++},
        {"method", method}
    };

    if (!params.is_null()) {
        request["params"] = params;
    }

    auto result = transport_->send(request);
    if (!result.ok()) return result;

    // Check for JSON-RPC error
    auto& response = result.value();
    if (response.contains("error")) {
        auto& error = response["error"];
        std::string msg = error.contains("message") ? error["message"].get<std::string>() : "Unknown error";
        int code = error.contains("code") ? error["code"].get<int>() : -1;
        return Error::net("MCP error " + std::to_string(code) + ": " + msg);
    }

    if (response.contains("result")) {
        return response["result"];
    }

    return Error::parse("MCP response missing 'result' field");
}

Result<std::vector<ToolSpec>> McpClient::list_tools() {
    auto result = send_request("tools/list", nlohmann::json::object());
    if (!result.ok()) return result.error();

    std::vector<ToolSpec> tools;
    auto& data = result.value();

    if (!data.contains("tools") || !data["tools"].is_array()) {
        return Error::parse("MCP tools/list response missing 'tools' array");
    }

    for (const auto& tool : data["tools"]) {
        ToolSpec spec;
        spec.name = tool.value("name", "");
        spec.description = tool.value("description", "");
        spec.parameters = tool.value("inputSchema", nlohmann::json::object());
        if (!spec.name.empty()) {
            tools.push_back(std::move(spec));
        }
    }

    return tools;
}

Result<ToolResult> McpClient::call_tool(const std::string& name,
                                          const nlohmann::json& arguments) {
    nlohmann::json params = {
        {"name", name},
        {"arguments", arguments.is_null() ? nlohmann::json::object() : arguments}
    };

    auto result = send_request("tools/call", params);
    if (!result.ok()) return result.error();

    auto& data = result.value();
    bool is_error = data.value("isError", false);

    // Extract text content
    std::string output;
    if (data.contains("content") && data["content"].is_array()) {
        for (const auto& item : data["content"]) {
            if (item.value("type", "") == "text") {
                if (!output.empty()) output += "\n";
                output += item.value("text", "");
            }
        }
    } else if (data.contains("content") && data["content"].is_string()) {
        output = data["content"].get<std::string>();
    }

    if (output.empty() && is_error) {
        output = "MCP tool error (no message)";
    }

    return ToolResult{"", output, is_error};
}

bool McpClient::is_connected() const {
    return connected_;
}

}  // namespace ea::mcp
```

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpClient.h src/mcp/McpClient.cpp
git commit -m "feat(mcp): add McpClient with JSON-RPC 2.0 protocol implementation"
```

---

### Task 4: McpToolAdapter

**Files:**
- Create: `src/mcp/McpToolAdapter.h`

- [ ] **Step 1: Write McpToolAdapter.h (header-only)**

```cpp
// src/mcp/McpToolAdapter.h
#pragma once
#include "core/ITool.h"
#include "McpClient.h"
#include <memory>

namespace ea::mcp {

class McpToolAdapter : public ITool {
public:
    McpToolAdapter(std::shared_ptr<McpClient> client, ToolSpec spec, bool dangerous = false)
        : client_(std::move(client)), spec_(std::move(spec)), dangerous_(dangerous) {}

    std::string name() const override { return spec_.name; }
    std::string description() const override { return spec_.description; }
    nlohmann::json parameters_schema() const override { return spec_.parameters; }

    Result<ToolResult> execute(const nlohmann::json& args) override {
        return client_->call_tool(spec_.name, args);
    }

    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return dangerous_; }

private:
    std::shared_ptr<McpClient> client_;
    ToolSpec spec_;
    bool dangerous_;
};

}  // namespace ea::mcp
```

- [ ] **Step 2: Commit**

```bash
git add src/mcp/McpToolAdapter.h
git commit -m "feat(mcp): add McpToolAdapter wrapping MCP tools as ITool"
```

---

### Task 5: MCP Module CMakeLists + Top-level Integration

**Files:**
- Create: `src/mcp/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `include/ea/build_config.h.in`

- [ ] **Step 1: Write src/mcp/CMakeLists.txt**

```cmake
add_library(ea-mcp OBJECT
    StdioTransport.cpp
    McpClient.cpp
)
ea_target_compile_options(ea-mcp)
target_include_directories(ea-mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-mcp PUBLIC ea-core ea-common)
```

- [ ] **Step 2: Update top-level CMakeLists.txt**

Add after the existing `add_subdirectory(src/agent)` line:

```cmake
add_subdirectory(src/mcp)
```

Add `EA_ENABLE_MCP` option after the existing subsystem toggles:

```cmake
option(EA_ENABLE_MCP "Enable MCP client" ON)
```

Add embedded mode auto-configuration:

```cmake
if(EA_MODE_EMBEDDED)
    set(EA_ENABLE_MCP OFF CACHE BOOL "Disable MCP for embedded" FORCE)
endif()
```

Add `ea-mcp` object library to `embedded-agent-core`:

```cmake
add_library(embedded-agent-core STATIC
    $<TARGET_OBJECTS:ea-common>
    $<TARGET_OBJECTS:ea-provider>
    $<TARGET_OBJECTS:ea-tool>
    $<TARGET_OBJECTS:ea-memory>
    $<TARGET_OBJECTS:ea-agent>
    $<TARGET_OBJECTS:ea-platform>
    $<TARGET_OBJECTS:ea-config>
    $<TARGET_OBJECTS:ea-security>
    $<TARGET_OBJECTS:ea-mcp>
)
```

- [ ] **Step 3: Update include/ea/build_config.h.in**

Add after the existing `#cmakedefine` lines:

```
#cmakedefine EA_ENABLE_MCP
```

- [ ] **Step 4: Build to verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc)`

- [ ] **Step 5: Commit**

```bash
git add src/mcp/CMakeLists.txt CMakeLists.txt include/ea/build_config.h.in
git commit -m "feat(mcp): add MCP module build and EA_ENABLE_MCP compile flag"
```

---

### Task 6: Config Extension

**Files:**
- Modify: `src/config/Config.h`
- Modify: `src/config/Config.cpp`

- [ ] **Step 1: Add McpServerConfig to Config.h**

Before `AppConfig`, add:

```cpp
struct McpServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool dangerous = false;
};
```

Add `std::vector<McpServerConfig> mcp_servers;` to `AppConfig`.

- [ ] **Step 2: Add MCP parsing to Config.cpp**

After the existing security parsing block, add:

```cpp
        if (data.contains("mcp")) {
            auto mcp = toml::find(data, "mcp");
            if (mcp.contains("servers")) {
                auto servers = toml::find<std::vector<toml::table>>(mcp, "servers");
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
                            sc.env[k] = toml::find<std::string>(toml::find(server, "env"), k);
                        }
                    }
                    sc.dangerous = toml::find_or<bool>(server, "dangerous", false);
                    cfg.mcp_servers.push_back(std::move(sc));
                }
            }
        }
```

- [ ] **Step 3: Build and test**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`

- [ ] **Step 4: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp
git commit -m "feat(config): add McpServerConfig and [[mcp.servers]] TOML parsing"
```

---

### Task 7: main.cpp Integration

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update main.cpp**

Add includes:
```cpp
#include "mcp/McpClient.h"
#include "mcp/StdioTransport.h"
#include "mcp/McpToolAdapter.h"
```

After tool registration (step 7), add step 7.5:

```cpp
    // 7.5. Connect MCP servers and register their tools
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
            client->disconnect();
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
```

Note: `mcp_clients` must outlive `AgentLoop` — they're kept in `main` scope.

- [ ] **Step 2: Build and test**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate MCP client into main.cpp with Toolset registration"
```

---

### Task 8: Tests

**Files:**
- Create: `tests/test_mcp_client.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write test file**

```cpp
// tests/test_mcp_client.cpp
#include <catch2/catch_test_macros.hpp>
#include "mcp/ITransport.h"
#include "mcp/McpClient.h"
#include "mcp/McpToolAdapter.h"

using namespace ea::mcp;

// MockTransport for unit testing
class MockTransport : public ITransport {
public:
    nlohmann::json next_response;
    std::vector<nlohmann::json> sent_requests;
    bool fail_start = false;

    Result<void> start() override {
        if (fail_start) return ea::Error::io("start failed");
        running_ = true; return {};
    }
    Result<void> stop() override { running_ = false; return {}; }
    Result<nlohmann::json> send(const nlohmann::json& request) override {
        sent_requests.push_back(request);
        return next_response;
    }
    bool is_running() const override { return running_; }

private:
    bool running_ = false;
};

TEST_CASE("McpClient connect sends initialize request", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test-server"}, {"version", "1.0"}}}
    };

    McpClient client(std::unique_ptr<ITransport>(transport));
    auto result = client.connect();

    REQUIRE(result.ok());
    REQUIRE(client.is_connected());
    REQUIRE(transport->sent_requests.size() >= 1);
    REQUIRE(transport->sent_requests[0]["method"] == "initialize");
    REQUIRE(transport->sent_requests[0]["params"]["protocolVersion"] == "2024-11-05");
}

TEST_CASE("McpClient connect fails on transport error", "[mcp]") {
    auto transport = new MockTransport();
    transport->fail_start = true;

    McpClient client(std::unique_ptr<ITransport>(transport));
    auto result = client.connect();
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(client.is_connected());
}

TEST_CASE("McpClient list_tools returns ToolSpec vector", "[mcp]") {
    auto transport = new MockTransport();

    // First call: initialize
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    McpClient client(std::unique_ptr<ITransport>(transport));
    client.connect();

    // Second call: tools/list
    transport->next_response = {
        {"tools", nlohmann::json::array({
            {{"name", "read_file"}, {"description", "Read a file"}, {"inputSchema", nlohmann::json::object()}},
            {{"name", "write_file"}, {"description", "Write a file"}, {"inputSchema", nlohmann::json::object()}}
        })}
    };

    auto result = client.list_tools();
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    REQUIRE(result.value()[0].name == "read_file");
    REQUIRE(result.value()[1].name == "write_file");
}

TEST_CASE("McpClient call_tool sends tools/call and maps response", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    McpClient client(std::unique_ptr<ITransport>(transport));
    client.connect();

    transport->next_response = {
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "file content here"}}
        })},
        {"isError", false}
    };

    auto result = client.call_tool("read_file", nlohmann::json{{"path", "/tmp/test"}});
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "file content here");
    REQUIRE(result.value().is_error == false);
}

TEST_CASE("McpClient call_tool maps isError to ToolResult", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    McpClient client(std::unique_ptr<ITransport>(transport));
    client.connect();

    transport->next_response = {
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "Permission denied"}}
        })},
        {"isError", true}
    };

    auto result = client.call_tool("read_file", nlohmann::json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == true);
    REQUIRE(result.value().output == "Permission denied");
}

TEST_CASE("McpToolAdapter delegates execute to McpClient", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    client->connect();

    ea::ToolSpec spec{"test_tool", "A test tool", nlohmann::json::object()};
    McpToolAdapter adapter(client, spec, true);

    REQUIRE(adapter.name() == "test_tool");
    REQUIRE(adapter.description() == "A test tool");
    REQUIRE(adapter.is_dangerous() == true);
    REQUIRE(adapter.is_mutating() == true);

    transport->next_response = {
        {"content", nlohmann::json::array({
            {{"type", "text"}, {"text", "result"}}
        })},
        {"isError", false}
    };

    auto result = adapter.execute(nlohmann::json::object());
    REQUIRE(result.ok());
    REQUIRE(result.value().output == "result");
}

TEST_CASE("McpToolAdapter is_dangerous defaults to false", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    auto client = std::make_shared<McpClient>(std::unique_ptr<ITransport>(transport));
    client->connect();

    ea::ToolSpec spec{"safe_tool", "Safe", nlohmann::json::object()};
    McpToolAdapter adapter(client, spec);  // dangerous defaults to false

    REQUIRE(adapter.is_dangerous() == false);
}

TEST_CASE("McpClient disconnect stops transport", "[mcp]") {
    auto transport = new MockTransport();
    transport->next_response = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"serverInfo", {{"name", "test"}, {"version", "1.0"}}}
    };

    McpClient client(std::unique_ptr<ITransport>(transport));
    client.connect();
    REQUIRE(client.is_connected());

    client.disconnect();
    REQUIRE_FALSE(client.is_connected());
    REQUIRE_FALSE(transport->is_running());
}
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

Add `test_mcp_client.cpp` to the source list and add `ea-mcp` to `target_link_libraries`.

- [ ] **Step 3: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[mcp]" --reporter compact`

- [ ] **Step 4: Commit**

```bash
git add tests/test_mcp_client.cpp tests/CMakeLists.txt
git commit -m "test(mcp): add MCP client and adapter unit tests"
```

---

### Task 9: Final Verification

- [ ] **Step 1: Clean rebuild + full test suite**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) && ./tests/ea-tests --reporter compact`

- [ ] **Step 2: Verify no compiler warnings**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep "warning:" | grep -v "mbedtls" | grep -v "third_party"`
