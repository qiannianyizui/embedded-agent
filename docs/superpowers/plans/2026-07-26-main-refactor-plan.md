# main.cpp 重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 650 行的 main.cpp 拆分为 AppBuilder + IRunner 策略 + CliCommandRegistry，使 main.cpp 降至 ~35 行，零 `#ifdef`，所有新模块可独立单元测试。

**Architecture:** AppBuilder 从 Config 构建 AppContext（持有所有运行时对象），IRunner 策略接口统一三种运行模式（CLI/TUI/Server），CliCommandRegistry 替代 if-else 命令链。`#ifdef` 仅出现在 IRunner::create() 工厂和 CMakeLists.txt 中。

**Tech Stack:** C++17, CMake OBJECT 库, Catch2 测试框架

## Global Constraints

- 命名空间：`ea::app`（新模块）
- 错误处理：使用 `Result<T>` + `Error` 工厂方法，不抛异常
- 编译选项：新目标使用 `ea_target_compile_options(target)`
- 测试标签：`[app]`
- 行为等价：重构后运行时行为与现有完全一致
- TDD：先写测试再实现

---

## File Structure

| 操作 | 文件 | 职责 |
|------|------|------|
| Create | `src/app/AppContext.h` | 运行时对象容器 |
| Create | `src/app/AppBuilder.h` | 工厂接口 |
| Create | `src/app/AppBuilder.cpp` | 从 Config 构建 AppContext |
| Create | `src/app/IRunner.h` | 运行模式策略接口 |
| Create | `src/app/IRunner.cpp` | 工厂方法 |
| Create | `src/app/CliRunner.h` | CLI 模式 Runner |
| Create | `src/app/CliRunner.cpp` | CLI 交互循环 |
| Create | `src/app/CliCommandRegistry.h` | 命令注册表接口 |
| Create | `src/app/CliCommandRegistry.cpp` | 命令注册表实现 |
| Create | `src/app/TuiRunner.h` | TUI 模式 Runner |
| Create | `src/app/TuiRunner.cpp` | TUI 启动 |
| Create | `src/app/ServerRunner.h` | Server 模式 Runner |
| Create | `src/app/ServerRunner.cpp` | Server 启动 |
| Create | `src/app/SetupWizardRunner.h` | Setup 子命令 |
| Create | `src/app/SetupWizardRunner.cpp` | Setup 向导逻辑 |
| Create | `src/app/auto_resume.h` | 对话恢复函数 |
| Create | `src/app/auto_resume.cpp` | 对话恢复实现 |
| Create | `src/app/CMakeLists.txt` | app 模块构建 |
| Create | `src/agent/BudgetEventListener.h` | 从 main.cpp 内联 struct 提取 |
| Modify | `src/common/io/FileSystem.h` | 新增 `resolve_data_path()` |
| Modify | `src/common/io/FileSystem.cpp` | 实现 `resolve_data_path()` |
| Modify | `src/main.cpp` | 重写为 ~35 行 |
| Modify | `CMakeLists.txt` | 新增 ea-app, 更新链接 |
| Create | `tests/unit/test_app_builder.cpp` | AppBuilder 测试 |
| Create | `tests/unit/test_cli_command_registry.cpp` | 命令注册表测试 |
| Create | `tests/unit/test_auto_resume.cpp` | auto_resume 测试 |
| Modify | `tests/unit/CMakeLists.txt` | 新增测试文件 |

---

### Task 1: resolve_data_path 公共函数

提取 main.cpp 中 3 处重复的 data_dir 路径解析逻辑。

**Files:**
- Modify: `src/common/io/FileSystem.h`
- Modify: `src/common/io/FileSystem.cpp`
- Test: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `ea::fs::config_dir()`, `ea::fs::home_dir()`, `ea::fs::mkdir_p()`
- Produces: `ea::fs::resolve_data_path(const std::string& filename) -> Result<std::string>`

- [ ] **Step 1: 写失败测试**

在 `tests/unit/test_filesystem.cpp` 末尾追加：

```cpp
TEST_CASE("resolve_data_path returns config_dir/filename when config_dir ok", "[common][filesystem]") {
    auto result = ea::fs::resolve_data_path("test.db");
    REQUIRE(result.ok());
    // Should end with /test.db
    REQUIRE(result.value().size() >= 8);
    REQUIRE(result.value().substr(result.value().size() - 8) == "/test.db");
}

TEST_CASE("resolve_data_path with empty filename", "[common][filesystem]") {
    auto result = ea::fs::resolve_data_path("");
    // Empty filename should still produce a valid path ending in /
    REQUIRE(result.ok());
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) 2>&1 | tail -5`
Expected: 编译失败，`resolve_data_path` 未声明

- [ ] **Step 3: 在 FileSystem.h 声明**

在 `src/common/io/FileSystem.h` 的 `namespace ea::fs` 块中，`data_dir()` 声明之后追加：

```cpp
    // Resolve a data file path: prefers config_dir()/filename,
    // falls back to home_dir()/.embedded-agent/filename
    Result<std::string> resolve_data_path(const std::string& filename);
```

- [ ] **Step 4: 在 FileSystem.cpp 实现**

在 `src/common/io/FileSystem.cpp` 的 `data_dir()` 函数之后追加：

```cpp
Result<std::string> resolve_data_path(const std::string& filename) {
    auto cfg_dir = config_dir();
    if (cfg_dir.ok()) {
        return cfg_dir.value() + "/" + filename;
    }
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent/" + filename;
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd /home/lsy/embedded-agent/build && ./tests/unit/ea-unit-tests "[filesystem]" -v`
Expected: 所有 filesystem 测试 PASS

- [ ] **Step 6: 提交**

```bash
git add src/common/io/FileSystem.h src/common/io/FileSystem.cpp tests/unit/test_filesystem.cpp
git commit -m "feat(common): add resolve_data_path() to eliminate repeated path logic"
```

---

### Task 2: BudgetEventListener 提取

从 main.cpp 内联的 `struct BudgetEventListener` 提取为独立头文件。

**Files:**
- Create: `src/agent/BudgetEventListener.h`
- Modify: `src/agent/CMakeLists.txt` (新增头文件到源列表)

**Interfaces:**
- Consumes: `ea::agent::IEventListener`, `ea::agent::AgentEvent`, `ea::agent::AgentEventType`, `ea::budget::BudgetTracker`
- Produces: `ea::agent::BudgetEventListener` 类

- [ ] **Step 1: 创建 BudgetEventListener.h**

```cpp
// BudgetEventListener — prints usage/cost after each LLM call
#pragma once
#include "IEventListener.h"
#include "budget/BudgetTracker.h"
#include <iostream>
#include <iomanip>

namespace ea::agent {

class BudgetEventListener : public IEventListener {
public:
    explicit BudgetEventListener(budget::BudgetTracker* tracker) : tracker_(tracker) {}

    void on_event(const AgentEvent& event) override {
        if (event.type == AgentEventType::LLMResponse) {
            if (event.usage.input_tokens > 0 || event.usage.output_tokens > 0) {
                auto su = tracker_->session_usage();
                auto sc = tracker_->session_cost();
                auto old_flags = std::cout.flags();
                auto old_precision = std::cout.precision();
                std::cout << "\n[Usage: " << su.input_tokens << " in / "
                          << su.output_tokens << " out | $"
                          << std::fixed << std::setprecision(4) << sc.total()
                          << " session]" << std::flush;
                std::cout.flags(old_flags);
                std::cout.precision(old_precision);
            }
        }
    }

private:
    budget::BudgetTracker* tracker_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: 更新 agent CMakeLists.txt**

在 `src/agent/CMakeLists.txt` 的 `add_library(ea-agent OBJECT ...)` 源文件列表中确认 `BudgetEventListener.h` 存在（header-only 不需要加到源列表，但需确认 include 路径正确）。由于是 header-only，无需修改 CMakeLists.txt。

- [ ] **Step 3: 验证编译**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: 编译成功（新头文件尚未被引用，不影响现有代码）

- [ ] **Step 4: 提交**

```bash
git add src/agent/BudgetEventListener.h
git commit -m "feat(agent): extract BudgetEventListener from main.cpp inline struct"
```

---

### Task 3: AppContext 数据结构

定义持有所有运行时对象的数据结构。

**Files:**
- Create: `src/app/AppContext.h`

**Interfaces:**
- Consumes: 所有模块的核心类型（IProvider, SqliteMemory, SecurityPolicy, ToolRegistry, BudgetTracker, etc.）
- Produces: `ea::app::AppContext` struct

- [ ] **Step 1: 创建 src/app/ 目录**

Run: `mkdir -p /home/lsy/embedded-agent/src/app`

- [ ] **Step 2: 创建 AppContext.h**

```cpp
// AppContext — holds all runtime objects for the application
#pragma once
#include "config/Config.h"
#include "core/IProvider.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "agent/IMemoryStrategy.h"
#include "agent/SubagentOrchestrator.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "conversation/SqliteConversationStore.h"
#include "mcp/McpClient.h"
#include "common/net/HttpClient.h"
#include <memory>
#include <vector>
#include <string>

namespace ea::app {

struct AppContext {
    config::AppConfig config;

    // Provider layer
    std::shared_ptr<ea::IProvider> provider;
    std::shared_ptr<ea::budget::BudgetTracker> budget_tracker;
    ea::IProvider* effective_provider = nullptr;

    // Storage
    std::unique_ptr<ea::memory::SqliteMemory> memory;
    std::unique_ptr<ea::conversation::SqliteConversationStore> conversation_store;
    std::shared_ptr<ea::budget::SqliteUsageStore> usage_store;

    // Security
    std::unique_ptr<ea::security::SecurityPolicy> security;
    std::unique_ptr<ea::security::IApprovalHandler> approval;

    // Agent components
    std::unique_ptr<ea::agent::ContextCompressor> compressor;
    std::unique_ptr<ea::agent::IMemoryStrategy> memory_strategy;
    std::unique_ptr<ea::agent::SubagentOrchestrator> orchestrator;

    // Tools
    std::unique_ptr<ea::tool::ToolRegistry> registry;
    ea::net::HttpClient http_client;

    // MCP
    std::vector<std::shared_ptr<ea::mcp::McpClient>> mcp_clients;

    // Debug
    bool debug = false;
};

}  // namespace ea::app
```

- [ ] **Step 3: 验证编译**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: 编译成功（新头文件尚未被引用）

- [ ] **Step 4: 提交**

```bash
git add src/app/AppContext.h
git commit -m "feat(app): add AppContext struct for runtime object container"
```

---

### Task 4: AppBuilder 工厂

从 Config 构建 AppContext，封装 main.cpp 步骤 3~7.6 的所有初始化逻辑。

**Files:**
- Create: `src/app/AppBuilder.h`
- Create: `src/app/AppBuilder.cpp`
- Create: `src/app/CMakeLists.txt`
- Modify: `CMakeLists.txt` (根目录)
- Test: `tests/unit/test_app_builder.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `ea::config::AppConfig`, `ea::fs::resolve_data_path()`, `ea::provider::create()`, `ea::budget::BudgetTracker`, `ea::budget::SqliteUsageStore`, `ea::memory::SqliteMemory`, `ea::security::SecurityPolicy`, `ea::security::StdinApprovalHandler`, `ea::security::PendingApprovalHandler`, `ea::agent::ContextCompressor`, `ea::agent::ProgressiveMemoryStrategy`, `ea::conversation::SqliteConversationStore`, `ea::tool::ToolRegistry`, `ea::tool::ShellTool`, `ea::tool::FileTool`, `ea::tool::SearchTool`, `ea::tool::WebTool`, `ea::tool::MemoryTool`, `ea::mcp::StdioTransport`, `ea::mcp::McpClient`, `ea::mcp::McpToolAdapter`, `ea::agent::SubagentOrchestrator`, `ea::agent::DelegateTool`
- Produces: `ea::app::AppBuilder::build(const config::AppConfig&, bool) -> Result<AppContext>`

- [ ] **Step 1: 写失败测试**

创建 `tests/unit/test_app_builder.cpp`：

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/AppBuilder.h"
#include "config/Config.h"

using namespace ea::app;
using namespace ea::config;

TEST_CASE("AppBuilder rejects unknown provider type", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "nonexistent_provider";
    auto result = AppBuilder::build(cfg);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().message.find("Unknown provider") != std::string::npos);
}

TEST_CASE("AppBuilder builds with minimal config", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    // Use :memory: paths to avoid file I/O
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";
    cfg.budget.path = ":memory:";

    auto result = AppBuilder::build(cfg);
    REQUIRE(result.ok());
    auto& ctx = result.value();
    REQUIRE(ctx.provider != nullptr);
    REQUIRE(ctx.effective_provider != nullptr);
    REQUIRE(ctx.memory != nullptr);
    REQUIRE(ctx.security != nullptr);
    REQUIRE(ctx.registry != nullptr);
    REQUIRE(ctx.debug == false);
}

TEST_CASE("AppBuilder budget disabled when no pricing", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";
    cfg.budget.path = ":memory:";
    cfg.budget.pricing.clear();
    cfg.budget.warn_cost_usd = 0;

    auto result = AppBuilder::build(cfg);
    REQUIRE(result.ok());
    auto& ctx = result.value();
    REQUIRE(ctx.budget_tracker == nullptr);
    REQUIRE(ctx.effective_provider == ctx.provider.get());
}

TEST_CASE("AppBuilder budget enabled with pricing", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";
    cfg.budget.path = ":memory:";
    budget::ModelPricing mp;
    mp.input_per_mtok = 3.0;
    mp.output_per_mtok = 15.0;
    cfg.budget.pricing["gpt-4"] = mp;

    auto result = AppBuilder::build(cfg);
    REQUIRE(result.ok());
    auto& ctx = result.value();
    REQUIRE(ctx.budget_tracker != nullptr);
    REQUIRE(ctx.effective_provider == ctx.budget_tracker.get());
}

TEST_CASE("AppBuilder debug flag preserved", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";

    auto result = AppBuilder::build(cfg, true);
    REQUIRE(result.ok());
    REQUIRE(result.value().debug == true);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) 2>&1 | tail -5`
Expected: 编译失败，`AppBuilder` 未声明

- [ ] **Step 3: 创建 AppBuilder.h**

```cpp
// AppBuilder — constructs AppContext from AppConfig
#pragma once
#include "app/AppContext.h"
#include "common/base/Result.h"

namespace ea::app {

class AppBuilder {
public:
    static ea::Result<AppContext> build(const ea::config::AppConfig& cfg, bool debug = false);
};

}  // namespace ea::app
```

- [ ] **Step 4: 创建 AppBuilder.cpp**

```cpp
#include "app/AppBuilder.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "tool/ShellTool.h"
#include "tool/FileTool.h"
#include "tool/SearchTool.h"
#include "tool/WebTool.h"
#include "tool/MemoryTool.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "security/StdinApprovalHandler.h"
#include "security/PendingApprovalHandler.h"
#include "agent/ContextCompressor.h"
#include "agent/ProgressiveMemoryStrategy.h"
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
#include "mcp/McpClient.h"
#include "mcp/StdioTransport.h"
#include "mcp/McpToolAdapter.h"
#include "budget/BudgetTracker.h"
#include "budget/SqliteUsageStore.h"
#include "conversation/SqliteConversationStore.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "ea/build_config.h"

namespace ea::app {

ea::Result<AppContext> AppBuilder::build(const ea::config::AppConfig& cfg, bool debug) {
    AppContext ctx;
    ctx.config = cfg;
    ctx.debug = debug;

    // 1. Create provider
    auto provider_raw = ea::provider::create(cfg.provider);
    if (!provider_raw) {
        return ea::Error::invalid_arg("Unknown provider type: " + cfg.provider.type);
    }
    ctx.provider = std::shared_ptr<ea::IProvider>(provider_raw);

    // 2. Create budget tracker (wraps provider if budget is enabled)
    if (!cfg.budget.pricing.empty() || cfg.budget.warn_cost_usd > 0) {
        std::string usage_path = cfg.budget.path;
        if (usage_path.empty()) {
            auto rp = ea::fs::resolve_data_path("usage.db");
            usage_path = rp.ok() ? rp.value() : "usage.db";
        }

        ctx.usage_store = std::make_shared<ea::budget::SqliteUsageStore>(
            ea::budget::SqliteUsageStore::Config{usage_path});
        auto usage_open = ctx.usage_store->open();
        if (!usage_open.ok()) {
            EA_WARN("Usage store open failed: {}", usage_open.error().message);
            ctx.usage_store.reset();
        }

        ctx.budget_tracker = std::make_shared<ea::budget::BudgetTracker>(
            ctx.provider, cfg.budget);
        if (ctx.usage_store) {
            ctx.budget_tracker->set_store(ctx.usage_store);
        }
    }

    ctx.effective_provider = ctx.budget_tracker
        ? static_cast<ea::IProvider*>(ctx.budget_tracker.get())
        : ctx.provider.get();

    // 3. Create memory
    std::string memory_path = cfg.memory.path;
    if (memory_path.empty()) {
        auto rp = ea::fs::resolve_data_path("memory.db");
        memory_path = rp.ok() ? rp.value() : "memory.db";
        if (rp.ok()) {
            ea::fs::mkdir_p(rp.value().substr(0, rp.value().rfind('/')));
        }
    }
    ctx.memory = std::make_unique<ea::memory::SqliteMemory>(
        ea::memory::SqliteMemory::Config{memory_path, cfg.memory.enable_fts5});

    // 4. Create security policy
    ctx.security = std::make_unique<ea::security::SecurityPolicy>();
    if (!cfg.security.workspace.empty()) {
        ctx.security->set_workspace(cfg.security.workspace);
    }
    if (!cfg.security.allowed_commands.empty()) {
        ctx.security->set_allowed_commands(cfg.security.allowed_commands);
    }

    // 5. Create approval handler
    if (ctx.security->level() == ea::security::AutonomyLevel::Full
        && cfg.security.auto_approve_dangerous) {
        // Full mode + auto-approve = no approval needed
    } else if (cfg.security.approval_mode == "auto") {
#if defined(EA_MODE_SERVER)
        ctx.approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
#elif defined(EA_MODE_CLI)
        ctx.approval = std::make_unique<ea::security::StdinApprovalHandler>();
#else
        // Embedded mode: no interactive interface
#endif
    } else if (cfg.security.approval_mode == "stdin") {
        ctx.approval = std::make_unique<ea::security::StdinApprovalHandler>();
    } else if (cfg.security.approval_mode == "pending") {
        ctx.approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
    }

    // 6. Create context compressor
    if (cfg.agent.compression_enable) {
        ea::agent::CompressionConfig comp_cfg;
        comp_cfg.max_tokens = cfg.agent.compression_max_tokens;
        comp_cfg.keep_recent_turns = cfg.agent.compression_keep_recent_turns;
        ctx.compressor = std::make_unique<ea::agent::ContextCompressor>(
            ctx.effective_provider, comp_cfg);
    }

    // 7. Create memory strategy
    if (cfg.memory_strategy.type == "progressive") {
        ea::agent::ProgressiveMemoryConfig strat_cfg;
        strat_cfg.working_turns = cfg.memory_strategy.working_turns;
        strat_cfg.short_term_max = cfg.memory_strategy.short_term_max;
        strat_cfg.long_term_importance = cfg.memory_strategy.long_term_importance;
        strat_cfg.enable_fact_extraction = cfg.memory_strategy.enable_fact_extraction;
        strat_cfg.enable_auto_summarize = cfg.memory_strategy.enable_auto_summarize;
        ctx.memory_strategy = std::make_unique<ea::agent::ProgressiveMemoryStrategy>(strat_cfg);
    }

    // 8. Create conversation store
    std::string conv_path = cfg.conversation.path;
    if (conv_path.empty()) {
        auto rp = ea::fs::resolve_data_path("conversations.db");
        conv_path = rp.ok() ? rp.value() : "conversations.db";
    }
    ctx.conversation_store = std::make_unique<ea::conversation::SqliteConversationStore>(
        ea::conversation::SqliteConversationStore::Config{conv_path});
    auto conv_open = ctx.conversation_store->open();
    if (!conv_open.ok()) {
        EA_ERROR("Conversation store open failed: {}", conv_open.error().message);
        ctx.conversation_store.reset();
    }

    // 9. Register tools
    ctx.registry = std::make_unique<ea::tool::ToolRegistry>();
    ctx.registry->register_tool(std::make_unique<ea::tool::ShellTool>());
    ctx.registry->register_tool(std::make_unique<ea::tool::FileTool>());
    ctx.registry->register_tool(std::make_unique<ea::tool::SearchTool>());
    ctx.registry->register_tool(std::make_unique<ea::tool::WebTool>(&ctx.http_client));
    ctx.registry->register_tool(std::make_unique<ea::tool::MemoryTool>(ctx.memory.get()));

    // 10. Connect MCP servers
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
        ctx.registry->register_toolset(std::move(toolset));
        ctx.mcp_clients.push_back(client);

        EA_INFO("MCP server '{}' connected with {} tools", server_cfg.name, tools_result.value().size());
    }

    // 11. Create subagent orchestrator
    ctx.orchestrator = std::make_unique<ea::agent::SubagentOrchestrator>(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get());

    for (auto& sub_cfg : cfg.agent.subagents) {
        EA_INFO("Registering subagent template: {}", sub_cfg.name);
        ctx.orchestrator->register_template(sub_cfg);
    }

    if (!cfg.agent.subagents.empty()) {
        ctx.registry->register_tool(std::make_unique<ea::agent::DelegateTool>(ctx.orchestrator.get()));
    }

    return ctx;
}

}  // namespace ea::app
```

- [ ] **Step 5: 创建 src/app/CMakeLists.txt**

```cmake
add_library(ea-app OBJECT
    AppBuilder.cpp
)

target_include_directories(ea-app PUBLIC ${CMAKE_SOURCE_DIR}/src)
ea_target_compile_options(ea-app)
target_link_libraries(ea-app PUBLIC
    ea-core ea-common ea-config ea-provider ea-memory ea-security
    ea-tool ea-agent ea-mcp ea-budget ea-conversation ea-platform
)
```

- [ ] **Step 6: 更新根 CMakeLists.txt**

在 `add_subdirectory(src/server)` 之后追加：

```cmake
add_subdirectory(src/app)
```

在 `embedded-agent-core` 的源文件列表中追加：

```cmake
    $<TARGET_OBJECTS:ea-app>
```

- [ ] **Step 7: 更新 tests/unit/CMakeLists.txt**

在 `ea-unit-tests` 源文件列表中追加 `test_app_builder.cpp`，在 `target_link_libraries` 中追加 `ea-app`。

- [ ] **Step 8: 运行测试确认通过**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) && ./tests/unit/ea-unit-tests "[app]" -v`
Expected: 所有 `[app]` 测试 PASS

- [ ] **Step 9: 提交**

```bash
git add src/app/AppBuilder.h src/app/AppBuilder.cpp src/app/CMakeLists.txt \
        tests/unit/test_app_builder.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "feat(app): add AppBuilder factory to construct AppContext from Config"
```

---

### Task 5: CliCommandRegistry 命令注册表

用注册表模式替代 main.cpp 中的 if-else 命令链。

**Files:**
- Create: `src/app/CliCommandRegistry.h`
- Create: `src/app/CliCommandRegistry.cpp`
- Test: `tests/unit/test_cli_command_registry.cpp`
- Modify: `src/app/CMakeLists.txt`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: 无外部依赖（纯逻辑）
- Produces: `ea::app::CliCommandRegistry`, `ea::app::CliCommand`, `ea::app::CommandFn`

- [ ] **Step 1: 写失败测试**

创建 `tests/unit/test_cli_command_registry.cpp`：

```cpp
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include "app/CliCommandRegistry.h"

using namespace ea::app;

TEST_CASE("CliCommandRegistry dispatches registered command", "[app]") {
    CliCommandRegistry reg;
    bool executed = false;
    std::string captured_args;

    reg.register_command({"test", "/test", "Test command",
        [&](const std::string& args) { executed = true; captured_args = args; }});

    REQUIRE(reg.try_dispatch("/test"));
    REQUIRE(executed);
    REQUIRE(captured_args == "");
}

TEST_CASE("CliCommandRegistry dispatches command with args", "[app]") {
    CliCommandRegistry reg;
    std::string captured_args;

    reg.register_command({"resume", "/resume <id>", "Resume conversation",
        [&](const std::string& args) { captured_args = args; }});

    REQUIRE(reg.try_dispatch("/resume abc123"));
    REQUIRE(captured_args == "abc123");
}

TEST_CASE("CliCommandRegistry returns false for non-command input", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"test", "/test", "Test command",
        [](const std::string&) {}});

    REQUIRE_FALSE(reg.try_dispatch("hello world"));
    REQUIRE_FALSE(reg.try_dispatch(""));
}

TEST_CASE("CliCommandRegistry returns false for unknown command", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"test", "/test", "Test command",
        [](const std::string&) {}});

    REQUIRE_FALSE(reg.try_dispatch("/unknown"));
}

TEST_CASE("CliCommandRegistry print_help lists commands", "[app]") {
    CliCommandRegistry reg;
    reg.register_command({"usage", "/usage", "Show usage", [](const std::string&) {}});
    reg.register_command({"cost", "/cost", "Show cost", [](const std::string&) {}});

    // Redirect cout
    std::ostringstream oss;
    auto* old_buf = std::cout.rdbuf(oss.rdbuf());
    reg.print_help();
    std::cout.rdbuf(old_buf);

    std::string output = oss.str();
    REQUIRE(output.find("/usage") != std::string::npos);
    REQUIRE(output.find("Show usage") != std::string::npos);
    REQUIRE(output.find("/cost") != std::string::npos);
    REQUIRE(output.find("Show cost") != std::string::npos);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) 2>&1 | tail -5`
Expected: 编译失败

- [ ] **Step 3: 创建 CliCommandRegistry.h**

```cpp
// CliCommandRegistry — extensible slash-command dispatcher
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace ea::app {

using CommandFn = std::function<void(const std::string& args)>;

struct CliCommand {
    std::string name;         // e.g. "usage", "cost", "history"
    std::string usage;        // e.g. "/usage [global]"
    std::string description;  // e.g. "Show token usage statistics"
    CommandFn execute;
};

class CliCommandRegistry {
public:
    void register_command(CliCommand cmd);

    // Try to dispatch input as a command. Returns true if handled.
    bool try_dispatch(const std::string& input) const;

    // Print help listing all registered commands
    void print_help() const;

private:
    std::vector<CliCommand> commands_;

    // Parse "/name args" → name + args. Returns false if not a command.
    static bool parse_command(const std::string& input,
                             std::string& name,
                             std::string& args);
};

}  // namespace ea::app
```

- [ ] **Step 4: 创建 CliCommandRegistry.cpp**

```cpp
#include "app/CliCommandRegistry.h"
#include <iostream>

namespace ea::app {

void CliCommandRegistry::register_command(CliCommand cmd) {
    commands_.push_back(std::move(cmd));
}

bool CliCommandRegistry::parse_command(const std::string& input,
                                        std::string& name,
                                        std::string& args) {
    if (input.empty() || input[0] != '/') return false;

    auto space_pos = input.find(' ');
    if (space_pos == std::string::npos) {
        name = input.substr(1);  // strip leading '/'
        args = "";
    } else {
        name = input.substr(1, space_pos - 1);
        args = input.substr(space_pos + 1);
    }
    return !name.empty();
}

bool CliCommandRegistry::try_dispatch(const std::string& input) const {
    std::string name, args;
    if (!parse_command(input, name, args)) return false;

    for (const auto& cmd : commands_) {
        if (cmd.name == name) {
            cmd.execute(args);
            return true;
        }
    }
    return false;
}

void CliCommandRegistry::print_help() const {
    std::cout << "Available commands:" << std::endl;
    for (const auto& cmd : commands_) {
        std::cout << "  " << cmd.usage << "  —  " << cmd.description << std::endl;
    }
}

}  // namespace ea::app
```

- [ ] **Step 5: 更新 src/app/CMakeLists.txt**

在 `add_library(ea-app OBJECT` 源文件列表中追加 `CliCommandRegistry.cpp`。

- [ ] **Step 6: 更新 tests/unit/CMakeLists.txt**

在 `ea-unit-tests` 源文件列表中追加 `test_cli_command_registry.cpp`。

- [ ] **Step 7: 运行测试确认通过**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) && ./tests/unit/ea-unit-tests "[app]" -v`
Expected: 所有 `[app]` 测试 PASS

- [ ] **Step 8: 提交**

```bash
git add src/app/CliCommandRegistry.h src/app/CliCommandRegistry.cpp \
        tests/unit/test_cli_command_registry.cpp src/app/CMakeLists.txt \
        tests/unit/CMakeLists.txt
git commit -m "feat(app): add CliCommandRegistry for extensible slash-command dispatch"
```

---

### Task 6: auto_resume 共用函数

提取 CLI 和 TUI 中重复的"恢复上次对话"逻辑。

**Files:**
- Create: `src/app/auto_resume.h`
- Create: `src/app/auto_resume.cpp`
- Test: `tests/unit/test_auto_resume.cpp`
- Modify: `src/app/CMakeLists.txt`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `ea::app::AppContext`, `ea::agent::AgentLoop`
- Produces: `ea::app::auto_resume(AppContext&, agent::AgentLoop&, bool)`

- [ ] **Step 1: 写失败测试**

创建 `tests/unit/test_auto_resume.cpp`：

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/auto_resume.h"
#include "app/AppContext.h"
#include "agent/AgentLoop.h"
#include "config/Config.h"

using namespace ea::app;

TEST_CASE("auto_resume does nothing when conversation_store is null", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();
    // Force conversation_store to null
    ctx.conversation_store.reset();

    // Create a minimal AgentLoop (no callbacks needed for this test)
    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    // Should not crash
    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id().empty());
}

TEST_CASE("auto_resume does nothing when auto_resume config is false", "[app]") {
    AppConfig cfg;
    cfg.provider.type = "openai_compatible";
    cfg.provider.base_url = "https://api.example.com";
    cfg.provider.api_key = "test-key";
    cfg.provider.default_model = "gpt-4";
    cfg.memory.path = ":memory:";
    cfg.conversation.path = ":memory:";
    cfg.conversation.auto_resume = false;

    auto ctx_result = AppBuilder::build(cfg);
    REQUIRE(ctx_result.ok());
    auto& ctx = ctx_result.value();

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{},
        [](const std::string&) {},
        nullptr);

    auto_resume(ctx, loop, true);
    REQUIRE(loop.conversation_id().empty());
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) 2>&1 | tail -5`
Expected: 编译失败

- [ ] **Step 3: 创建 auto_resume.h**

```cpp
// auto_resume — shared conversation resume logic for CLI and TUI modes
#pragma once
#include "agent/AgentLoop.h"

namespace ea::app {

struct AppContext;  // forward declare to avoid heavy include

// Resume the last conversation if auto_resume is enabled.
// silent=true suppresses the "Resumed: ..." message (TUI handles its own display).
void auto_resume(AppContext& ctx, ea::agent::AgentLoop& loop, bool silent = false);

}  // namespace ea::app
```

- [ ] **Step 4: 创建 auto_resume.cpp**

```cpp
#include "app/auto_resume.h"
#include "app/AppContext.h"
#include "common/io/Logger.h"
#include <iostream>

namespace ea::app {

void auto_resume(AppContext& ctx, ea::agent::AgentLoop& loop, bool silent) {
    if (!ctx.conversation_store || !ctx.config.conversation.auto_resume) return;

    auto recent = ctx.conversation_store->list(1, 0);
    if (!recent.ok() || recent.value().empty()) return;

    auto& meta = recent.value()[0];
    auto msgs = ctx.conversation_store->load(meta.id);
    if (!msgs.ok() || msgs.value().empty()) return;

    loop.restore_conversation(meta.id, std::move(msgs.value()));
    if (!silent) {
        std::cout << "Resumed: " << meta.title
                  << " (" << meta.message_count << " messages)" << std::endl;
    }
}

}  // namespace ea::app
```

- [ ] **Step 5: 更新 src/app/CMakeLists.txt**

在源文件列表中追加 `auto_resume.cpp`。

- [ ] **Step 6: 更新 tests/unit/CMakeLists.txt**

在源文件列表中追加 `test_auto_resume.cpp`。

- [ ] **Step 7: 运行测试确认通过**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --target ea-unit-tests -j$(nproc) && ./tests/unit/ea-unit-tests "[app]" -v`
Expected: 所有 `[app]` 测试 PASS

- [ ] **Step 8: 提交**

```bash
git add src/app/auto_resume.h src/app/auto_resume.cpp \
        tests/unit/test_auto_resume.cpp src/app/CMakeLists.txt \
        tests/unit/CMakeLists.txt
git commit -m "feat(app): add auto_resume() to deduplicate conversation resume logic"
```

---

### Task 7: IRunner 策略接口 + CliRunner

创建运行模式策略接口和 CLI 模式实现。

**Files:**
- Create: `src/app/IRunner.h`
- Create: `src/app/IRunner.cpp`
- Create: `src/app/CliRunner.h`
- Create: `src/app/CliRunner.cpp`
- Modify: `src/app/CMakeLists.txt`

**Interfaces:**
- Consumes: `ea::app::AppContext`, `ea::agent::AgentLoop`, `ea::app::CliCommandRegistry`, `ea::app::auto_resume`, `ea::agent::BudgetEventListener`, `ea::agent::LoggingEventListener`
- Produces: `ea::app::IRunner`, `ea::app::CliRunner`, `ea::app::IRunner::create()`

- [ ] **Step 1: 创建 IRunner.h**

```cpp
// IRunner — strategy interface for application run modes
#pragma once
#include <memory>
#include <string>

namespace ea::app {

struct AppContext;

class IRunner {
public:
    virtual ~IRunner() = default;
    virtual int run(AppContext& ctx) = 0;

    // Factory: select Runner based on compile-time mode
    static std::unique_ptr<IRunner> create();
};

}  // namespace ea::app
```

- [ ] **Step 2: 创建 IRunner.cpp**

```cpp
#include "app/IRunner.h"
#include "app/CliRunner.h"
#include "ea/build_config.h"

#if defined(EA_MODE_SERVER)
#include "app/ServerRunner.h"
#elif defined(EA_ENABLE_TUI)
#include "app/TuiRunner.h"
#endif

namespace ea::app {

std::unique_ptr<IRunner> IRunner::create() {
#if defined(EA_MODE_SERVER)
    return std::make_unique<ServerRunner>();
#elif defined(EA_ENABLE_TUI)
    return std::make_unique<TuiRunner>();
#else
    return std::make_unique<CliRunner>();
#endif
}

}  // namespace ea::app
```

- [ ] **Step 3: 创建 CliRunner.h**

```cpp
// CliRunner — CLI interactive loop with command dispatch
#pragma once
#include "app/IRunner.h"
#include "app/CliCommandRegistry.h"
#include <memory>

namespace ea::app {

class CliRunner : public IRunner {
public:
    int run(AppContext& ctx) override;

private:
    std::unique_ptr<CliCommandRegistry> commands_;
    void register_commands(AppContext& ctx);
};

}  // namespace ea::app
```

- [ ] **Step 4: 创建 CliRunner.cpp**

```cpp
#include "app/CliRunner.h"
#include "app/AppContext.h"
#include "app/auto_resume.h"
#include "agent/AgentLoop.h"
#include "agent/LoggingEventListener.h"
#include "agent/BudgetEventListener.h"
#include "common/io/Logger.h"
#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>

namespace ea::app {

void CliRunner::register_commands(AppContext& ctx) {
    commands_ = std::make_unique<CliCommandRegistry>();

    commands_->register_command({
        "quit", "/quit", "Exit the agent",
        [](const std::string&) { /* handled by return value */ }
    });
    commands_->register_command({
        "exit", "/exit", "Exit the agent",
        [](const std::string&) { /* handled by return value */ }
    });
    commands_->register_command({
        "usage", "/usage [global]", "Show token usage statistics",
        [&ctx](const std::string& args) {
            if (!ctx.budget_tracker) {
                std::cout << "Budget tracking not enabled" << std::endl;
                return;
            }
            if (args == "global") {
                auto gu = ctx.budget_tracker->global_usage();
                std::cout << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            } else {
                auto su = ctx.budget_tracker->session_usage();
                auto gu = ctx.budget_tracker->global_usage();
                std::cout << "Session Usage:\n"
                          << "  Input:  " << su.input_tokens << " tokens\n"
                          << "  Output: " << su.output_tokens << " tokens\n"
                          << "  Cache:  " << su.cache_read_tokens << " read / "
                          << su.cache_write_tokens << " write\n"
                          << "  Total:  " << su.total_tokens() << " tokens\n\n"
                          << "Global Usage:\n"
                          << "  Input:  " << gu.input_tokens << " tokens\n"
                          << "  Output: " << gu.output_tokens << " tokens\n"
                          << "  Total:  " << gu.total_tokens() << " tokens" << std::endl;
            }
        }
    });
    commands_->register_command({
        "cost", "/cost [global]", "Show cost breakdown",
        [&ctx](const std::string& args) {
            if (!ctx.budget_tracker) {
                std::cout << "Budget tracking not enabled" << std::endl;
                return;
            }
            auto old_flags = std::cout.flags();
            auto old_precision = std::cout.precision();
            if (args == "global") {
                auto gc = ctx.budget_tracker->global_cost();
                std::cout << "Global Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << gc.total() << std::endl;
            } else {
                auto sc = ctx.budget_tracker->session_cost();
                auto gc = ctx.budget_tracker->global_cost();
                std::cout << "Session Cost:\n"
                          << "  Total: $" << std::fixed << std::setprecision(4) << sc.total() << "\n\n"
                          << "Global Cost:\n"
                          << "  Total: $" << gc.total() << std::endl;
            }
            std::cout.flags(old_flags);
            std::cout.precision(old_precision);
        }
    });
    commands_->register_command({
        "history", "/history", "List recent conversations",
        [&ctx](const std::string&) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            auto list = ctx.conversation_store->list(10, 0);
            if (list.ok()) {
                for (const auto& m : list.value()) {
                    std::cout << "  " << m.id << "  " << m.title
                              << "  (" << m.message_count << " msgs, " << m.updated_at << ")" << std::endl;
                }
            }
        }
    });
    commands_->register_command({
        "resume", "/resume <id>", "Resume a conversation",
        [&ctx](const std::string& id) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            // Note: resume from command needs the loop — handled in run()
        }
    });
    commands_->register_command({
        "export", "/export", "Export current conversation as JSONL",
        [&ctx](const std::string&) {
            // Note: needs loop — handled in run()
        }
    });
    commands_->register_command({
        "import", "/import <file>", "Import conversation from JSONL file",
        [&ctx](const std::string& filepath) {
            if (!ctx.conversation_store) {
                std::cout << "Conversation persistence not available" << std::endl;
                return;
            }
            std::ifstream file(filepath);
            if (!file.is_open()) {
                std::cout << "Cannot open file: " << filepath << std::endl;
                return;
            }
            std::string jsonl_data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
            auto new_id = ctx.conversation_store->import_jsonl(jsonl_data);
            if (new_id.ok()) {
                std::cout << "Imported conversation: " << new_id.value() << std::endl;
            } else {
                std::cout << "Import failed: " << new_id.error().message << std::endl;
            }
        }
    });
    commands_->register_command({
        "help", "/help", "Show available commands",
        [this](const std::string&) { commands_->print_help(); }
    });
}

int CliRunner::run(AppContext& ctx) {
    register_commands(ctx);

    // Create AgentLoop with CLI callbacks
    ea::agent::AgentLoop::StreamFn stream_fn;
    if (ctx.config.agent.stream) {
        stream_fn = [](const ea::StreamChunk& chunk) {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                std::cout << chunk.data << std::flush;
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                std::cout << std::endl;
            }
        };
    }

    ea::agent::AgentLoop loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{
            ctx.config.agent.max_iterations, 65536, 100, true,
            ctx.config.agent.stream, ctx.config.conversation.auto_persist
        },
        [](const std::string& text) { std::cout << text << std::endl; },
        stream_fn,
        ctx.security.get(),
        ctx.approval.get(),
        ctx.compressor.get(),
        ctx.memory_strategy.get(),
        ctx.conversation_store.get(),
        ctx.budget_tracker.get()
    );

    // Add event listeners
    if (ctx.debug) {
        loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }
    if (ctx.budget_tracker) {
        loop.add_listener(std::make_shared<ea::agent::BudgetEventListener>(ctx.budget_tracker.get()));
    }

    // Auto-resume
    auto_resume(ctx, loop, false);

    // Interactive loop
    std::cout << "embedded-agent v0.1.0 (type /help for commands, /quit to exit)" << std::endl;

    std::string input;
    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        // Handle quit/exit directly (need to break the loop)
        if (input == "/quit" || input == "/exit") break;

        // Handle /resume with loop reference
        if (input.size() > 8 && input.substr(0, 8) == "/resume ") {
            if (ctx.conversation_store) {
                std::string cid = input.substr(8);
                auto msgs = ctx.conversation_store->load(cid);
                if (msgs.ok()) {
                    loop.restore_conversation(cid, std::move(msgs.value()));
                    auto meta = ctx.conversation_store->get_meta(cid);
                    std::cout << "Resumed: " << (meta.ok() ? meta.value().title : cid) << std::endl;
                } else {
                    std::cout << "Conversation not found: " << cid << std::endl;
                }
            }
            continue;
        }

        // Handle /export with loop reference
        if (input == "/export") {
            if (ctx.conversation_store && !loop.conversation_id().empty()) {
                auto data = ctx.conversation_store->export_jsonl(loop.conversation_id());
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

        // Try command registry for other commands
        if (commands_->try_dispatch(input)) continue;

        // Run agent
        auto result = loop.run(input);
        if (ctx.budget_tracker && !loop.conversation_id().empty()) {
            ctx.budget_tracker->set_session_id(loop.conversation_id());
        }
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }

    return 0;
}

}  // namespace ea::app
```

- [ ] **Step 5: 更新 src/app/CMakeLists.txt**

在源文件列表中追加 `IRunner.cpp` 和 `CliRunner.cpp`。

- [ ] **Step 6: 验证编译**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: 编译成功（IRunner.cpp 中的 ServerRunner/TuiRunner include 在非对应模式下不会触发，因为 `#ifdef` 保护）

- [ ] **Step 7: 提交**

```bash
git add src/app/IRunner.h src/app/IRunner.cpp src/app/CliRunner.h src/app/CliRunner.cpp \
        src/app/CMakeLists.txt
git commit -m "feat(app): add IRunner strategy interface and CliRunner implementation"
```

---

### Task 8: TuiRunner + ServerRunner + SetupWizardRunner

创建 TUI 和 Server 模式的 Runner，以及 Setup 子命令独立入口。

**Files:**
- Create: `src/app/TuiRunner.h`
- Create: `src/app/TuiRunner.cpp`
- Create: `src/app/ServerRunner.h`
- Create: `src/app/ServerRunner.cpp`
- Create: `src/app/SetupWizardRunner.h`
- Create: `src/app/SetupWizardRunner.cpp`
- Modify: `src/app/CMakeLists.txt`

**Interfaces:**
- Consumes: `ea::app::AppContext`, `ea::tui::TuiApp`, `ea::server::HttpServer`, `ea::tui::SetupWizard`, `ea::config::load/save`
- Produces: `ea::app::TuiRunner`, `ea::app::ServerRunner`, `ea::app::SetupWizardRunner`, `ea::app::SetupArgs`

- [ ] **Step 1: 创建 TuiRunner.h**

```cpp
// TuiRunner — FTXUI-based interactive interface
#pragma once
#include "app/IRunner.h"

namespace ea::app {

class TuiRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
```

- [ ] **Step 2: 创建 TuiRunner.cpp**

```cpp
#include "app/TuiRunner.h"
#include "app/AppContext.h"
#include "app/auto_resume.h"
#include "agent/AgentLoop.h"
#include "agent/LoggingEventListener.h"
#include "agent/BudgetEventListener.h"
#include "tui/TuiApp.h"
#include "common/io/Logger.h"

namespace ea::app {

int TuiRunner::run(AppContext& ctx) {
    ea::tui::TuiApp tui;

    auto tui_output = tui.output_fn();
    auto tui_stream = tui.stream_fn();

    ea::agent::AgentLoop tui_loop(
        ctx.effective_provider, ctx.registry.get(), ctx.memory.get(),
        ea::agent::AgentLoop::Config{
            ctx.config.agent.max_iterations, 65536, 100, true,
            ctx.config.agent.stream, ctx.config.conversation.auto_persist
        },
        tui_output,
        tui_stream,
        ctx.security.get(),
        tui.approval_handler(),
        ctx.compressor.get(),
        ctx.memory_strategy.get(),
        ctx.conversation_store.get(),
        ctx.budget_tracker.get()
    );

    if (ctx.debug) {
        tui_loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }

    auto_resume(ctx, tui_loop, true);

    tui.run(tui_loop, ctx.budget_tracker.get(), ctx.conversation_store.get());
    return 0;
}

}  // namespace ea::app
```

- [ ] **Step 3: 创建 ServerRunner.h**

```cpp
// ServerRunner — HTTP API server mode
#pragma once
#include "app/IRunner.h"

namespace ea::app {

class ServerRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
```

- [ ] **Step 4: 创建 ServerRunner.cpp**

```cpp
#include "app/ServerRunner.h"
#include "app/AppContext.h"
#include "server/HttpServer.h"
#include "common/io/Logger.h"
#include <chrono>

namespace ea::app {

int ServerRunner::run(AppContext& ctx) {
    ea::server::ServerConfig srv_cfg;
    srv_cfg.host = ctx.config.server.host;
    srv_cfg.port = ctx.config.server.port;
    srv_cfg.max_sessions = ctx.config.server.max_sessions;
    srv_cfg.cors_origin = ctx.config.server.cors_origin;
    srv_cfg.session_idle_timeout = std::chrono::seconds(ctx.config.server.session_idle_timeout);

    auto http_server = std::make_unique<ea::server::HttpServer>(
        srv_cfg, ctx.effective_provider, ctx.registry.get(), ctx.security.get(),
        ctx.memory.get(), ctx.conversation_store.get(), ctx.budget_tracker.get()
    );

    EA_INFO("Server starting on {}:{}", srv_cfg.host, srv_cfg.port);
    http_server->start();
    return 0;
}

}  // namespace ea::app
```

- [ ] **Step 5: 创建 SetupWizardRunner.h**

```cpp
// SetupWizardRunner — setup subcommand entry point
#pragma once
#include <string>

namespace ea::app {

struct SetupArgs {
    bool non_interactive = false;
    bool reset = false;
    std::string config_path;
};

class SetupWizardRunner {
public:
    static int run(const SetupArgs& args);
};

}  // namespace ea::app
```

- [ ] **Step 6: 创建 SetupWizardRunner.cpp**

```cpp
#include "app/SetupWizardRunner.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "config/Config.h"
#include "ea/build_config.h"
#include <iostream>

#ifdef EA_ENABLE_TUI
#include "tui/SetupWizard.h"
#endif

namespace ea::app {

int SetupWizardRunner::run(const SetupArgs& args) {
    auto home = ea::platform::home_dir();
    ea::log::init(home + "/.embedded-agent", false);

    if (args.non_interactive) {
        auto cfg = ea::tui::run_non_interactive_setup();
        if (!args.config_path.empty()) cfg.config_path = args.config_path;

        auto save_result = ea::config::save(cfg);
        if (!save_result.ok()) {
            std::cerr << "Error saving config: " << save_result.error().message << std::endl;
            return 1;
        }

        std::cout << "✓ Configuration saved to " << cfg.config_path << std::endl;
        return 0;
    }

#ifdef EA_ENABLE_TUI
    ea::tui::WizardState state;

    std::string cfg_path = args.config_path;
    if (cfg_path.empty()) {
        auto cfg_dir = ea::fs::config_dir();
        if (cfg_dir.ok()) {
            cfg_path = cfg_dir.value() + "/config.toml";
        }
    }
    auto exists_result = ea::fs::exists(cfg_path);
    state.has_existing_config = exists_result.ok() && exists_result.value();

    std::string hermes_path = home + "/.hermes";
    auto hm_exists = ea::fs::exists(hermes_path);
    state.has_hermes = hm_exists.ok() && hm_exists.value();

    if (state.has_existing_config && !args.reset) {
        auto cfg_result = ea::config::load(cfg_path);
        if (cfg_result.ok()) {
            auto& cfg = cfg_result.value();
            state.provider_type = cfg.provider.type;
            state.base_url = cfg.provider.base_url;
            state.api_key = cfg.provider.api_key;
            state.default_model = cfg.provider.default_model.empty()
                ? cfg.agent.model : cfg.provider.default_model;
            state.workspace = cfg.security.workspace;
            state.autonomy = cfg.security.autonomy;
        }
    }

    auto wizard = ea::tui::make_setup_wizard(state);
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    state.exit_loop = screen.ExitLoopClosure();
    screen.Loop(wizard);

    if (state.cancelled) {
        std::cout << "Setup cancelled." << std::endl;
        return 0;
    }
    if (!state.finished) {
        return 0;
    }

    auto cfg = ea::tui::build_config_from_wizard(state);
    if (!args.config_path.empty()) cfg.config_path = args.config_path;

    auto save_result = ea::config::save(cfg);
    if (!save_result.ok()) {
        std::cerr << "Error saving config: " << save_result.error().message << std::endl;
        return 1;
    }

    std::cout << "✓ Configuration saved to " << cfg.config_path << std::endl;
    return 0;
#else
    std::cerr << "Setup wizard requires TUI support. "
              << "Rebuild with EA_ENABLE_TUI=ON or use --non-interactive." << std::endl;
    return 1;
#endif
}

}  // namespace ea::app
```

- [ ] **Step 7: 更新 src/app/CMakeLists.txt**

追加源文件，并添加条件编译：

```cmake
add_library(ea-app OBJECT
    AppBuilder.cpp
    IRunner.cpp
    CliRunner.cpp
    CliCommandRegistry.cpp
    SetupWizardRunner.cpp
    auto_resume.cpp
)

if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    target_sources(ea-app PRIVATE TuiRunner.cpp)
endif()

if(EA_MODE_SERVER)
    target_sources(ea-app PRIVATE ServerRunner.cpp)
endif()

target_include_directories(ea-app PUBLIC ${CMAKE_SOURCE_DIR}/src)
ea_target_compile_options(ea-app)
target_link_libraries(ea-app PUBLIC
    ea-core ea-common ea-config ea-provider ea-memory ea-security
    ea-tool ea-agent ea-mcp ea-budget ea-conversation ea-platform
)
```

- [ ] **Step 8: 验证编译**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: 编译成功

- [ ] **Step 9: 提交**

```bash
git add src/app/TuiRunner.h src/app/TuiRunner.cpp src/app/ServerRunner.h \
        src/app/ServerRunner.cpp src/app/SetupWizardRunner.h \
        src/app/SetupWizardRunner.cpp src/app/CMakeLists.txt
git commit -m "feat(app): add TuiRunner, ServerRunner, and SetupWizardRunner"
```

---

### Task 9: 重写 main.cpp

将 main.cpp 从 650 行缩减为 ~35 行，使用 AppBuilder + IRunner。

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `ea::app::AppBuilder`, `ea::app::IRunner`, `ea::app::SetupWizardRunner`, `ea::app::SetupArgs`, `ea::config::load`, `ea::platform::home_dir`, `ea::log::init`
- Produces: `main()` 函数

- [ ] **Step 1: 重写 main.cpp**

将 `src/main.cpp` 完整替换为：

```cpp
#include "app/AppBuilder.h"
#include "app/IRunner.h"
#include "app/SetupWizardRunner.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"
#include "config/Config.h"
#include <CLI/CLI.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent"};

    std::string config_path;
    bool debug = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("--debug", debug, "Enable debug logging");

    // Setup subcommand
    auto setup_cmd = app.add_subcommand("setup", "Interactive setup wizard");
    bool setup_non_interactive = false;
    bool setup_reset = false;
    setup_cmd->add_flag("--non-interactive", setup_non_interactive,
                        "Non-interactive mode (use defaults/env vars)");
    setup_cmd->add_flag("--reset", setup_reset, "Reset config to defaults");

    CLI11_PARSE(app, argc, argv);

    // Handle setup subcommand
    if (setup_cmd->parsed()) {
        return ea::app::SetupWizardRunner::run({
            setup_non_interactive, setup_reset, config_path
        });
    }

    // Initialize logging
    auto home = ea::platform::home_dir();
    ea::log::init(home + "/.embedded-agent", debug);
    EA_INFO("embedded-agent v0.1.0 starting");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }

    // Build application context
    auto ctx_result = ea::app::AppBuilder::build(cfg_result.value(), debug);
    if (!ctx_result.ok()) {
        EA_ERROR("App init failed: {}", ctx_result.error().message);
        std::cerr << "Init error: " << ctx_result.error().message << std::endl;
        return 1;
    }

    // Select and run mode
    auto runner = ea::app::IRunner::create();
    int rc = runner->run(ctx_result.value());

    EA_INFO("embedded-agent shutting down");
    return rc;
}
```

- [ ] **Step 2: 验证编译**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: 编译成功

- [ ] **Step 3: 运行全部单元测试**

Run: `cd /home/lsy/embedded-agent/build && ./tests/unit/ea-unit-tests`
Expected: 所有测试 PASS

- [ ] **Step 4: 提交**

```bash
git add src/main.cpp
git commit -m "refactor(app): rewrite main.cpp from 650 to ~35 lines using AppBuilder + IRunner"
```

---

### Task 10: 端到端验证

确保重构后应用行为与之前完全一致。

**Files:**
- 无新文件

- [ ] **Step 1: 运行全部单元测试**

Run: `cd /home/lsy/embedded-agent/build && ./tests/unit/ea-unit-tests`
Expected: 所有测试 PASS

- [ ] **Step 2: 运行集成测试（如果存在）**

Run: `cd /home/lsy/embedded-agent/build && ./tests/integration/ea-integration-tests 2>/dev/null || echo "No integration tests"`
Expected: 所有集成测试 PASS

- [ ] **Step 3: 运行系统测试（如果存在）**

Run: `cd /home/lsy/embedded-agent/build && ./tests/system/ea-system-tests 2>/dev/null || echo "No system tests"`
Expected: 所有系统测试 PASS

- [ ] **Step 4: 验证 CLI 启动**

Run: `cd /home/lsy/embedded-agent/build && echo "/quit" | ./embedded-agent --config /tmp/test-refactor.toml 2>&1 || true`
Expected: 应用正常启动并退出，无崩溃

- [ ] **Step 5: 验证 setup --non-interactive**

Run: `cd /home/lsy/embedded-agent/build && ./embedded-agent setup --non-interactive --config /tmp/test-refactor-setup.toml 2>&1 || true`
Expected: 输出 "✓ Configuration saved"

- [ ] **Step 6: 最终提交（如有修复）**

```bash
git add -A
git commit -m "fix(app): address end-to-end verification issues from main.cpp refactor"
```

如果没有修复需要，跳过此步。

---

## Self-Review

**1. Spec coverage:**
- AppContext → Task 3 ✅
- AppBuilder → Task 4 ✅
- resolve_data_path → Task 1 ✅
- IRunner + CliRunner → Task 7 ✅
- TuiRunner → Task 8 ✅
- ServerRunner → Task 8 ✅
- CliCommandRegistry → Task 5 ✅
- SetupWizardRunner → Task 8 ✅
- auto_resume → Task 6 ✅
- BudgetEventListener → Task 2 ✅
- main.cpp 重写 → Task 9 ✅
- CMake 集成 → Task 4, 8 ✅
- 测试 → Tasks 1, 4, 5, 6 ✅
- 端到端验证 → Task 10 ✅

**2. Placeholder scan:** 无 TBD/TODO/占位符 ✅

**3. Type consistency:**
- `AppBuilder::build()` 返回 `ea::Result<AppContext>` — 与 `ea::Result<T>` 模板一致 ✅
- `IRunner::create()` 返回 `std::unique_ptr<IRunner>` — 与 Runner 子类一致 ✅
- `CliCommandRegistry::try_dispatch()` 返回 `bool` — 与设计一致 ✅
- `auto_resume(AppContext&, AgentLoop&, bool)` — 参数类型与 AppContext 和 AgentLoop 一致 ✅
- `SetupWizardRunner::run(SetupArgs)` — SetupArgs 字段与 main.cpp 原有参数一致 ✅
