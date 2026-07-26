# main.cpp 重构设计：Builder + Runner + CommandRegistry

**日期**: 2026-07-26
**状态**: Draft

## 问题

`src/main.cpp` 有 650 行，`main()` 函数 ~515 行，承担了全部应用编排职责：

- CLI 参数解析
- 配置加载
- 所有组件创建和组装（provider、memory、security、tools、MCP、subagent、budget、conversation）
- 三种运行模式的 `#ifdef` 分支（Server / TUI / CLI）
- CLI 交互循环和 7 个斜杠命令的 if-else 链
- 内联类定义（BudgetEventListener）

**具体问题**：

1. **不可测试** — 所有初始化逻辑嵌在 main() 中，无法单元测试
2. **重复代码** — `data_dir` 路径拼接出现 3 次，`auto_resume` 逻辑出现 2 次，`/usage` 和 `/cost` 格式化代码高度重复
3. **编译耦合** — `#ifdef EA_MODE_SERVER` / `#ifdef EA_ENABLE_TUI` 散落在 main 中间
4. **职责混杂** — 一个函数同时负责构建、运行、命令分发

## 目标

- **可测试性**：AppBuilder、CliCommandRegistry、各 Runner 均可独立单元测试
- **可读性**：main.cpp 从 650 行降至 ~35 行，零 `#ifdef`，零内联类
- **可扩展性**：新增运行模式或 CLI 命令只需添加新类，不改现有代码
- **行为不变**：重构后运行时行为与现有完全一致

## 方案：Builder + Runner + CommandRegistry

### 架构总览

```
main.cpp (~35行)
  → parse_args()
  → SetupWizardRunner::run()        (setup 子命令)
  → config::load()
  → AppBuilder::build(config)       → AppContext
  → IRunner::create(mode)           → unique_ptr<IRunner>
  → runner->run(ctx)
```

### 模块划分

#### AppContext — 运行时对象容器

`src/app/AppContext.h`

持有应用运行所需的所有对象。不是 Service Locator，而是显式数据结构，所有字段一目了然。

```cpp
namespace ea::app {

struct AppContext {
    config::AppConfig config;

    std::shared_ptr<IProvider> provider;
    std::shared_ptr<budget::BudgetTracker> budget_tracker;
    IProvider* effective_provider = nullptr;

    std::unique_ptr<memory::SqliteMemory> memory;
    std::unique_ptr<conversation::SqliteConversationStore> conversation_store;
    std::shared_ptr<budget::SqliteUsageStore> usage_store;

    std::unique_ptr<security::SecurityPolicy> security;
    std::unique_ptr<security::IApprovalHandler> approval;

    std::unique_ptr<agent::ContextCompressor> compressor;
    std::unique_ptr<agent::IMemoryStrategy> memory_strategy;
    std::unique_ptr<agent::SubagentOrchestrator> orchestrator;

    std::unique_ptr<tool::ToolRegistry> registry;
    net::HttpClient http_client;

    std::vector<std::shared_ptr<mcp::McpClient>> mcp_clients;

    bool debug = false;
};

}  // namespace ea::app
```

**关键决策**：
- 所有权用 `unique_ptr` / `shared_ptr` 明确，AppContext 析构时自动清理
- `effective_provider` 是裸指针，指向 budget_tracker 或 provider
- 不持有 AgentLoop——AgentLoop 的创建依赖运行模式（CLI/TUI 各自创建不同回调），由 Runner 负责

#### AppBuilder — 从 Config 构建 AppContext

`src/app/AppBuilder.h` / `src/app/AppBuilder.cpp`

纯工厂，无状态，输入 Config 输出 `Result<AppContext>`。封装当前 main.cpp 步骤 3~7.6 的所有初始化逻辑。

```cpp
namespace ea::app {

class AppBuilder {
public:
    static Result<AppContext> build(const config::AppConfig& cfg, bool debug = false);

private:
    static Result<std::shared_ptr<IProvider>> create_provider(const config::AppConfig& cfg);
    static void create_budget(const config::AppConfig& cfg,
                              std::shared_ptr<IProvider> provider,
                              AppContext& ctx);
    static void create_memory(const config::AppConfig& cfg, AppContext& ctx);
    static void create_security(const config::AppConfig& cfg, AppContext& ctx);
    static void create_agent_components(const config::AppConfig& cfg, AppContext& ctx);
    static void create_tools(AppContext& ctx);
    static void connect_mcp(const config::AppConfig& cfg, AppContext& ctx);
    static void create_subagents(const config::AppConfig& cfg, AppContext& ctx);
};

}  // namespace ea::app
```

**关键决策**：
- `build()` 是静态工厂方法，构建是一次性操作
- 每个步骤是独立的 private 方法，通过友元可在测试中单独调用
- 路径解析逻辑提取为 `ea::fs::resolve_data_path()` 公共函数，消除 3 处重复
- 构建失败返回 `Error`，不抛异常，与项目 `Result<T>` 风格一致
- MCP 连接失败不阻断构建，只记日志并跳过

#### resolve_data_path — 公共路径解析

`src/common/io/FileSystem.h` / `.cpp` 新增

```cpp
namespace ea::fs {
    Result<std::string> resolve_data_path(const std::string& filename);
    // 优先 config_dir()/filename，fallback ~/.embedded-agent/filename
}
```

消除 budget usage_path、memory_path、conv_path 三处重复的路径解析逻辑。

#### IRunner — 运行模式策略接口

`src/app/IRunner.h` / `src/app/IRunner.cpp`

```cpp
namespace ea::app {

class IRunner {
public:
    virtual ~IRunner() = default;
    virtual int run(AppContext& ctx) = 0;

    static std::unique_ptr<IRunner> create(const std::string& mode = "");
};

}  // namespace ea::app
```

`create()` 工厂实现中 `#ifdef` 只出现在此一处：

```cpp
std::unique_ptr<IRunner> IRunner::create(const std::string& mode) {
#if defined(EA_MODE_SERVER)
    return std::make_unique<ServerRunner>();
#elif defined(EA_ENABLE_TUI)
    return std::make_unique<TuiRunner>();
#else
    return std::make_unique<CliRunner>();
#endif
}
```

#### CliRunner

`src/app/CliRunner.h` / `src/app/CliRunner.cpp`

CLI 模式：交互循环 + 命令注册表分发。

```cpp
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

交互循环：

```cpp
int CliRunner::run(AppContext& ctx) {
    register_commands(ctx);
    auto loop = create_agent_loop(ctx);

    auto_resume(ctx, loop);

    bool running = true;
    while (running) {
        std::cout << "\n> " << std::flush;
        std::string input;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (commands_->try_dispatch(input)) continue;

        auto result = loop.run(input);
        // 错误处理...
    }
    return 0;
}
```

#### TuiRunner

`src/app/TuiRunner.h` / `src/app/TuiRunner.cpp`

TUI 模式：FTXUI 启动。仅在 `EA_ENABLE_TUI` 编译时编译此文件。

```cpp
namespace ea::app {

class TuiRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
```

- 创建 TUI 专属 AgentLoop（使用 TUI 回调）
- 调用 `auto_resume()` 恢复对话

#### ServerRunner

`src/app/ServerRunner.h` / `src/app/ServerRunner.cpp`

Server 模式：HTTP API 启动。仅在 `EA_MODE_SERVER` 编译时编译此文件。

```cpp
namespace ea::app {

class ServerRunner : public IRunner {
public:
    int run(AppContext& ctx) override;
};

}  // namespace ea::app
```

#### auto_resume — 共用对话恢复逻辑

`src/app/auto_resume.h` / `src/app/auto_resume.cpp`

```cpp
namespace ea::app {

void auto_resume(AppContext& ctx, agent::AgentLoop& loop, bool silent = false);
// silent=false 时打印 "Resumed: ..." 消息（CLI 模式）
// silent=true 时不打印（TUI 模式自行显示）

}  // namespace ea::app
```

提取 CLI 和 TUI 中重复的"恢复上次对话"逻辑。

#### CliCommandRegistry — 命令注册表

`src/app/CliCommandRegistry.h` / `src/app/CliCommandRegistry.cpp`

```cpp
namespace ea::app {

using CommandFn = std::function<void(const std::string& args)>;

struct CliCommand {
    std::string name;
    std::string usage;
    std::string description;
    CommandFn execute;
};

class CliCommandRegistry {
public:
    void register_command(CliCommand cmd);
    bool try_dispatch(const std::string& input) const;
    void print_help() const;

private:
    std::vector<CliCommand> commands_;
    static bool parse_command(const std::string& input,
                             std::string& name,
                             std::string& args);
};

}  // namespace ea::app
```

**关键决策**：
- `try_dispatch()` 返回 bool——是命令返回 true，不是命令返回 false 让 AgentLoop 处理
- 命令通过 lambda 捕获 `AppContext&`
- 新增命令只需 `register_command()`，不改 CliRunner 主体
- `/help` 自动从注册表生成帮助文本

注册的命令：`/quit`、`/usage`、`/cost`、`/history`、`/resume`、`/export`、`/import`、`/help`

#### SetupWizardRunner

`src/app/SetupWizardRunner.h` / `src/app/SetupWizardRunner.cpp`

```cpp
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

逻辑与现有 `run_setup_wizard()` 完全一致。TUI 相关的 `#ifdef EA_ENABLE_TUI` 保留在此文件内部。

#### BudgetEventListener 提取

`src/agent/BudgetEventListener.h`

从 main.cpp 内联 struct 提取到 agent 模块：

```cpp
namespace ea::agent {

class BudgetEventListener : public IEventListener {
public:
    explicit BudgetEventListener(budget::BudgetTracker* tracker);
    void on_event(const AgentEvent& event) override;
private:
    budget::BudgetTracker* tracker_;
};

}  // namespace ea::agent
```

放在 `agent/` 而非 `app/`，因为它实现 `IEventListener` 接口，与 `TuiEventListener` 对等。

### 重构后的 main.cpp

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

    auto setup_cmd = app.add_subcommand("setup", "Interactive setup wizard");
    bool setup_non_interactive = false;
    bool setup_reset = false;
    setup_cmd->add_flag("--non-interactive", setup_non_interactive,
                        "Non-interactive mode (use defaults/env vars)");
    setup_cmd->add_flag("--reset", setup_reset, "Reset config to defaults");

    CLI11_PARSE(app, argc, argv);

    if (setup_cmd->parsed()) {
        return ea::app::SetupWizardRunner::run({
            setup_non_interactive, setup_reset, config_path
        });
    }

    auto home = ea::platform::home_dir();
    ea::log::init(home + "/.embedded-agent", debug);
    EA_INFO("embedded-agent v0.1.0 starting");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }

    auto ctx_result = ea::app::AppBuilder::build(cfg_result.value(), debug);
    if (!ctx_result.ok()) {
        EA_ERROR("App init failed: {}", ctx_result.error().message);
        std::cerr << "Init error: " << ctx_result.error().message << std::endl;
        return 1;
    }

    auto runner = ea::app::IRunner::create();
    int rc = runner->run(ctx_result.value());

    EA_INFO("embedded-agent shutting down");
    return rc;
}
```

**650 行 → ~35 行，零 `#ifdef`，零内联类定义。**

### CMake 集成

新增 `src/app/CMakeLists.txt`：

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

根 CMakeLists.txt 变更：
- 新增 `add_subdirectory(src/app)` 在 `add_subdirectory(src/server)` 之后
- `embedded-agent-core` 新增 `$<TARGET_OBJECTS:ea-app>`
- `ea-agent` OBJECT 库无需变更——`BudgetEventListener.h` 是 header-only，与 `LoggingEventListener.h` 风格一致

### 新文件总览

| 新文件 | 行数估算 | 来源 |
|--------|----------|------|
| `app/AppContext.h` | ~30 | 新建 |
| `app/AppBuilder.h` | ~25 | 新建 |
| `app/AppBuilder.cpp` | ~180 | 从 main.cpp 步骤 3~7.6 提取 |
| `app/IRunner.h` | ~15 | 新建 |
| `app/IRunner.cpp` | ~15 | 新建（含工厂方法） |
| `app/CliRunner.h` | ~15 | 新建 |
| `app/CliRunner.cpp` | ~80 | 从 main.cpp CLI 循环提取 |
| `app/CliCommandRegistry.h` | ~30 | 新建 |
| `app/CliCommandRegistry.cpp` | ~60 | 从 main.cpp 命令处理提取 |
| `app/TuiRunner.h` | ~10 | 新建 |
| `app/TuiRunner.cpp` | ~50 | 从 main.cpp TUI 分支提取 |
| `app/ServerRunner.h` | ~10 | 新建 |
| `app/ServerRunner.cpp` | ~25 | 从 main.cpp Server 分支提取 |
| `app/SetupWizardRunner.h` | ~15 | 新建 |
| `app/SetupWizardRunner.cpp` | ~80 | 从 main.cpp run_setup_wizard 提取 |
| `app/auto_resume.h` | ~10 | 新建 |
| `app/auto_resume.cpp` | ~20 | 从 CLI/TUI 重复逻辑提取 |
| `agent/BudgetEventListener.h` | ~20 | 从 main.cpp 内联 struct 提取 |
| `common/io/FileSystem.h/cpp` | +15 | 新增 `resolve_data_path()` |

## 测试策略

TDD 风格，先写测试再实现。测试 tag：`[app]`。

### AppBuilder 测试

| 测试场景 | 验证点 |
|----------|--------|
| 最小配置构建 | AppContext 所有字段非空/有效 |
| 未知 provider 类型 | 返回 Error，不崩溃 |
| Budget 启用 | budget_tracker 非空，effective_provider 指向 budget_tracker |
| Budget 禁用 | budget_tracker 为空，effective_provider 指向 provider |
| Memory 路径解析 | 优先 config_dir，fallback home |
| MCP 连接失败 | 不阻断构建，mcp_clients 为空，日志有 WARN |
| Subagent 配置为空 | orchestrator 存在但无模板，DelegateTool 未注册 |

### CliCommandRegistry 测试

| 测试场景 | 验证点 |
|----------|--------|
| 注册并分发命令 | try_dispatch 返回 true，回调被执行 |
| 非命令输入 | try_dispatch 返回 false |
| 命令带参数 | `/resume abc123` → name="resume", args="abc123" |
| 未知命令 | try_dispatch 返回 false |
| print_help | 输出包含所有已注册命令的 description |

### IRunner 工厂测试

| 测试场景 | 验证点 |
|----------|--------|
| 编译模式匹配 | create() 返回对应 Runner 子类 |

### resolve_data_path 测试

| 测试场景 | 验证点 |
|----------|--------|
| config_dir 可用 | 返回 config_dir()/filename |
| config_dir 不可用 | fallback ~/.embedded-agent/filename |

### BudgetEventListener 测试

| 测试场景 | 验证点 |
|----------|--------|
| LLMResponse 事件有 token | 输出包含 usage 信息 |
| 非 LLMResponse 事件 | 无输出 |
| token 为 0 | 无输出 |

### Mock 策略

- AppBuilder 测试：使用最小合法 Config（内存 provider、空 MCP 列表），不 mock 内部模块
- CliCommandRegistry 测试：用 lambda 计数器验证回调执行，不需要真实 AppContext
- BudgetEventListener 测试：mock BudgetTracker 的 `session_usage()` / `session_cost()`

## `#ifdef` 分布（重构后）

| 位置 | 条件 | 用途 |
|------|------|------|
| `IRunner.cpp` | `EA_MODE_SERVER` / `EA_ENABLE_TUI` | 工厂方法选择 Runner |
| `SetupWizardRunner.cpp` | `EA_ENABLE_TUI` | TUI 向导 vs 错误提示 |
| `CMakeLists.txt` | `EA_ENABLE_TUI` / `EA_MODE_SERVER` | 条件编译 TuiRunner/ServerRunner |

main.cpp 中 **零** `#ifdef`。

## 行为等价性

重构后运行时行为与现有完全一致：
- 相同的组件创建顺序和错误处理
- 相同的 CLI 命令集和输出格式
- 相同的 TUI/Server 启动流程
- 相同的 auto_resume 逻辑
- 相同的 BudgetEventListener 输出格式
