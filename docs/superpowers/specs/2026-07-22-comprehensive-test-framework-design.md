# 全面测试体系设计

> **日期：** 2026-07-22
> **状态：** 设计完成，待实施
> **范围：** 为 embedded-agent 建立完整的测试能力，覆盖功能、性能、安全、兼容性、可用性、回归、冒烟等测试类型

---

## 1. 背景与目标

### 1.1 现状

- **457 个测试用例**，全部在单一 `ea-tests` 二进制中
- 使用 Catch2 v3.7.1 框架
- 已有 `[integration]` 标签测试 18 个，但本质仍是 Mock 驱动的组件测试
- **缺失：** 性能基准、安全专项、模糊测试、冒烟测试、回归测试、兼容性测试、可用性测试
- **缺失：** 共享测试基础设施（15+ 处重复 Mock Provider 定义）
- **缺失：** Sanitizer 构建、代码覆盖率、分层 CI 管道

### 1.2 目标

建立全面的测试能力，支持以下测试类型：

| 类型 | 目的 | 运行频率 |
|------|------|----------|
| 单元测试 | 验证单个类/函数的正确性 | 每次提交 |
| 集成测试 | 验证跨模块交互的正确性 | 每次提交 |
| 系统测试 | 验证端到端流程在真实环境下的正确性 | 合并前 |
| 冒烟测试 | 30 秒内验证核心链路可用 | 每次提交 |
| 性能测试 | 测量关键路径延迟和吞吐 | 每日/手动 |
| 安全测试 | 验证安全策略、隔离、注入防护 | 每次提交 |
| 兼容性测试 | 验证不同构建模式/编译器/平台 | 合并前 |
| 回归测试 | 防止历史 Bug 复现 | 每次提交 |
| 可用性测试 | 验证公共 API 的易用性和正确性 | 合并前 |

---

## 2. 测试分层架构

### 2.1 测试二进制拆分

将单一 `ea-tests` 拆分为 5 个独立二进制：

| 二进制 | 源码目录 | 运行条件 | 预期用例数 | 运行时间目标 |
|--------|----------|----------|-----------|-------------|
| `ea-unit-tests` | `tests/unit/` | 每次提交 | ~350 | <10s |
| `ea-integration-tests` | `tests/integration/` | 每次提交 | ~50 | <30s |
| `ea-system-tests` | `tests/system/` | 合并前 | ~30 | <60s |
| `ea-bench-tests` | `tests/bench/` | 手动/每日 | ~20 | ~5min |
| `ea-security-tests` | `tests/security/` | 每次提交(ASan) | ~40 | <30s |

### 2.2 目录结构

```
tests/
├── common/                          # 共享测试基础设施
│   ├── CMakeLists.txt               # ea-test-common OBJECT 库
│   ├── MockProvider.h               # 统一 Mock Provider（声明）
│   ├── MockProvider.cpp             # 统一 Mock Provider（实现，避免 ODR）
│   ├── MockTool.h                   # 统一 Mock Tool（声明）
│   ├── MockTool.cpp                 # 统一 Mock Tool（实现）
│   ├── MockTransport.h              # MCP Mock Transport（声明）
│   ├── MockTransport.cpp            # MCP Mock Transport（实现）
│   ├── Fixtures.h                   # 通用 Fixture（header-only，struct 内联构造/析构）
│   ├── SecurityTestHelper.h         # 安全测试工具（header-only）
│   ├── FuzzHelper.h                 # 模糊测试语料生成（header-only）
│   ├── CompatTestHelper.h           # 兼容性测试工具（header-only）
│   └── TestHelpers.h                # 通用辅助函数（header-only）
├── unit/                            # 单元测试
│   ├── CMakeLists.txt
│   ├── test_common_result.cpp
│   ├── test_common_string.cpp
│   ├── test_common_json.cpp
│   ├── test_common_sse.cpp
│   ├── test_config.cpp
│   ├── test_build_config.cpp
│   ├── test_core_types.cpp
│   ├── test_filesystem.cpp
│   ├── test_memory_sqlite.cpp
│   ├── test_in_memory_backend.cpp
│   ├── test_memory_factory.cpp
│   ├── test_scoped_memory.cpp
│   ├── test_memory_manager.cpp
│   ├── test_provider_openai.cpp
│   ├── test_provider_anthropic.cpp
│   ├── test_provider_ollama.cpp
│   ├── test_reliable_provider.cpp
│   ├── test_router_provider.cpp
│   ├── test_credential_pool.cpp
│   ├── test_error_classifier.cpp
│   ├── test_prompt_guided_tools.cpp
│   ├── test_tool_registry.cpp
│   ├── test_toolset.cpp
│   ├── test_tool_shell.cpp
│   ├── test_tool_file.cpp
│   ├── test_tool_memory.cpp
│   ├── test_tool_web.cpp
│   ├── test_tool_search.cpp
│   ├── test_tool_output_config.cpp
│   ├── test_agent_loop.cpp
│   ├── test_system_prompt.cpp
│   ├── test_loop_detector.cpp
│   ├── test_context_compressor.cpp
│   ├── test_turn_context.cpp
│   ├── test_turn_steps.cpp
│   ├── test_subagent.cpp
│   ├── test_streaming.cpp
│   ├── test_events.cpp
│   ├── test_security_policy.cpp
│   ├── test_approval_handler.cpp
│   ├── test_stdin_approval_handler.cpp
│   ├── test_pending_approval_handler.cpp
│   ├── test_session_manager.cpp
│   ├── test_conversation_store.cpp
│   ├── test_budget_tracker.cpp
│   ├── test_usage_store.cpp
│   ├── test_mcp_client.cpp
│   ├── test_plugin_manifest.cpp
│   ├── test_plugin_loader.cpp
│   ├── test_plugin_tool_adapter.cpp
│   ├── test_plugin_host.cpp
│   ├── test_platform.cpp              # 新增：覆盖 platform 模块
│   ├── test_http_client.cpp           # 新增：覆盖 common/net
│   ├── test_retry_policy.cpp
│   └── test_regression.cpp            # 新增：回归测试
├── integration/                     # 集成测试
│   ├── CMakeLists.txt
│   ├── test_agent_memory.cpp
│   ├── test_provider_tool.cpp
│   ├── test_full_pipeline.cpp
│   ├── test_conversation_integration.cpp
│   ├── test_streaming_integration.cpp  # 新增
│   ├── test_plugin_integration.cpp     # 新增：插件全生命周期
│   ├── test_subagent_memory.cpp        # 新增：子 agent 记忆隔离
│   ├── test_budget_integration.cpp     # 新增：BudgetTracker + AgentLoop
│   └── test_fallback_integration.cpp   # 新增：Provider fallback 切换
├── system/                          # 系统测试
│   ├── CMakeLists.txt
│   ├── test_http_server.cpp
│   ├── test_smoke.cpp                  # 新增：冒烟测试
│   ├── test_cli_mode.cpp               # 新增：CLI 模式端到端
│   ├── test_server_mode.cpp            # 新增：Server 模式端到端
│   ├── test_embedded_mode.cpp          # 新增：Embedded 模式端到端
│   ├── test_build_config_compat.cpp    # 新增：构建配置兼容性
│   └── test_usability.cpp              # 新增：可用性测试
├── bench/                           # 性能基准测试
│   ├── CMakeLists.txt
│   ├── bench_agent_loop.cpp            # AgentLoop 单轮/多轮延迟
│   ├── bench_memory.cpp                # Memory recall/store 吞吐
│   ├── bench_tool_execute.cpp          # Tool 执行延迟
│   ├── bench_provider_parse.cpp        # Provider 响应解析
│   └── bench_concurrent.cpp            # 并发会话吞吐
├── security/                        # 安全测试
│   ├── CMakeLists.txt
│   ├── test_policy_boundary.cpp        # SecurityPolicy 边界测试
│   ├── test_tool_isolation.cpp         # 工具安全隔离
│   ├── test_plugin_sandbox.cpp         # 插件沙箱测试
│   ├── test_injection.cpp              # 注入攻击测试
│   ├── test_fuzz_json.cpp              # JSON 模糊测试
│   ├── test_fuzz_config.cpp            # 配置模糊测试
│   └── test_fuzz_provider.cpp          # Provider 响应模糊测试
└── plugins/
    └── mock_plugin/                 # 保持不变
```

### 2.3 CMake 构建结构

```cmake
# tests/CMakeLists.txt
option(ENABLE_TESTS "Build tests" OFF)
if(ENABLE_TESTS)
    FetchContent_Declare(catch2 URL ...)
    FetchContent_MakeAvailable(catch2)
    enable_testing()

    # 共享测试基础设施（OBJECT 库）
    add_subdirectory(common)

    # 分层测试二进制（各自独立开关）
    option(EA_ENABLE_UNIT_TESTS       "Build unit tests"       ON)
    option(EA_ENABLE_INTEGRATION_TESTS "Build integration tests" ON)
    option(EA_ENABLE_SYSTEM_TESTS     "Build system tests"     ON)
    option(EA_ENABLE_BENCH_TESTS      "Build benchmark tests"  OFF)
    option(EA_ENABLE_SECURITY_TESTS   "Build security tests"   ON)

    if(EA_ENABLE_UNIT_TESTS)
        add_subdirectory(unit)
    endif()
    if(EA_ENABLE_INTEGRATION_TESTS)
        add_subdirectory(integration)
    endif()
    if(EA_ENABLE_SYSTEM_TESTS)
        add_subdirectory(system)
    endif()
    if(EA_ENABLE_BENCH_TESTS)
        add_subdirectory(bench)
    endif()
    if(EA_ENABLE_SECURITY_TESTS)
        add_subdirectory(security)
    endif()
endif()
```

每个子目录的 CMakeLists.txt 独立定义自己的测试二进制：

```cmake
# tests/unit/CMakeLists.txt（示例）
add_executable(ea-unit-tests
    test_common_result.cpp
    test_memory_sqlite.cpp
    # ... 所有单元测试文件
)
target_link_libraries(ea-unit-tests PRIVATE
    ea-test-common
    ea-common ea-config ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
    ea-server ea-plugin
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_include_directories(ea-unit-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-unit-tests PRIVATE
    EA_MOCK_PLUGIN_PATH="${CMAKE_BINARY_DIR}/tests/plugins/mock_plugin/ea_mock_plugin.so"
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
include(CTest)
add_test(NAME ea-unit-tests COMMAND ea-unit-tests)
```

---

## 3. 共享测试基础设施

### 3.1 统一 Mock Provider

替代现有 15+ 处重复的 Mock Provider 定义。采用 .h+.cpp 分离模式，避免多测试二进制链接时的 ODR 违规：

```cpp
// tests/common/MockProvider.h
#pragma once
#include "core/IProvider.h"
#include <queue>
#include <functional>

namespace ea::test {

class MockProvider : public IProvider {
public:
    explicit MockProvider(
        std::string name = "mock",
        provider::ProviderCapabilities caps = {true, true, false, false, false}
    );

    // --- 响应队列 ---
    void enqueue(LLMResponse resp);
    void enqueue_text(std::string text, std::string stop_reason = "stop");
    void enqueue_tool_calls(std::vector<ToolCall> calls);
    void enqueue_error(Error err);
    void enqueue_chunks(std::vector<StreamChunk> chunks);

    // --- 观测能力 ---
    int call_count() const;
    int stream_call_count() const;
    const std::vector<Message>& last_messages() const;
    const std::vector<ToolSpec>& last_specs() const;
    const ChatOptions& last_options() const;

    // --- IProvider 接口 ---
    std::string name() const override;
    std::vector<std::string> list_models() const override;
    provider::ProviderCapabilities capabilities() const override;
    Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}
    ) override;
    Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}
    ) override;

    // --- 自定义行为 ---
    using ChatHandler = std::function<Result<LLMResponse>(
        const std::vector<Message>&, const std::vector<ToolSpec>&,
        const std::string&, const ChatOptions&)>;
    void set_chat_handler(ChatHandler handler);

private:
    std::string name_;
    provider::ProviderCapabilities caps_;
    std::queue<LLMResponse> responses_;
    std::queue<std::vector<StreamChunk>> chunk_queues_;
    ChatHandler custom_handler_;
    int call_count_ = 0;
    int stream_call_count_ = 0;
    std::vector<Message> last_messages_;
    std::vector<ToolSpec> last_specs_;
    ChatOptions last_options_;
};

}  // namespace ea::test
```

### 3.2 统一 Mock Tool

```cpp
// tests/common/MockTool.h
#pragma once
#include "core/ITool.h"
#include <functional>
#include <vector>

namespace ea::test {

class MockTool : public ITool {
public:
    explicit MockTool(
        std::string name,
        std::string output = "ok",
        bool mutating = true,
        bool dangerous = false
    );

    // 可配置行为
    void set_next_result(Result<ToolResult> result);
    void set_execute_handler(
        std::function<Result<ToolResult>(const json&)> handler
    );

    // 观测
    int call_count() const;
    const std::vector<json>& call_arguments() const;
    json last_argument() const;

    // ITool 接口
    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override;
    bool is_dangerous() const override;

private:
    std::string name_;
    std::string default_output_;
    bool mutating_;
    bool dangerous_;
    std::function<Result<ToolResult>(const json&)> handler_;
    int call_count_ = 0;
    std::vector<json> call_args_;
};

// 常用变体
class CountingTool : public MockTool {
public:
    explicit CountingTool(std::string name, std::string output = "ok");
};

class ErrorTool : public MockTool {
public:
    explicit ErrorTool(std::string name = "error_tool");
};

class SlowTool : public MockTool {
public:
    SlowTool(std::string name, std::chrono::milliseconds delay);
};

}  // namespace ea::test
```

### 3.3 通用 Fixture

```cpp
// tests/common/Fixtures.h
#pragma once
#include "MockProvider.h"
#include "MockTool.h"
#include "memory/InMemoryBackend.h"
#include "memory/SqliteMemory.h"
#include "memory/MemoryManager.h"
#include "tool/ToolRegistry.h"
#include "agent/AgentLoop.h"
#include "server/HttpServer.h"
#include "server/SessionManager.h"

namespace ea::test {

// 内存测试 Fixture
struct MemoryFixture {
    std::unique_ptr<InMemoryBackend> in_memory;
    std::unique_ptr<SqliteMemory> sqlite_mem;
    std::unique_ptr<MemoryManager> manager;

    MemoryFixture();
    ~MemoryFixture();
};

// 服务器测试 Fixture：启动 HTTP 服务器在随机端口
struct ServerFixture {
    std::unique_ptr<HttpServer> server;
    std::unique_ptr<SessionManager> sessions;
    std::string base_url;

    ServerFixture();
    ~ServerFixture();
};

// AgentLoop 测试 Fixture
struct AgentLoopFixture {
    MockProvider provider;
    ToolRegistry registry;
    std::unique_ptr<MemoryManager> memory;
    std::string last_output;
    std::unique_ptr<AgentLoop> loop;

    AgentLoopFixture();
    void run(std::string input);
};

}  // namespace ea::test
```

### 3.4 安全测试工具

```cpp
// tests/common/SecurityTestHelper.h
#pragma once
#include "security/SecurityPolicy.h"
#include <vector>
#include <string>

namespace ea::test {

struct InjectionPayloads {
    static std::vector<std::string> sql_injections();
    static std::vector<std::string> path_traversals();
    static std::vector<std::string> command_injections();
    static std::vector<std::string> json_injections();
    static std::vector<std::string> prompt_injections();
};

struct SecurityAssertions {
    static void assert_command_allowed(SecurityPolicy& policy, const std::string& cmd);
    static void assert_command_blocked(SecurityPolicy& policy, const std::string& cmd);
    static void assert_path_allowed(SecurityPolicy& policy, const std::string& path);
    static void assert_path_blocked(SecurityPolicy& policy, const std::string& path);
    static void assert_tool_allowed(SecurityPolicy& policy, const std::string& tool);
    static void assert_tool_blocked(SecurityPolicy& policy, const std::string& tool);
};

}  // namespace ea::test
```

### 3.5 模糊测试工具

```cpp
// tests/common/FuzzHelper.h
#pragma once
#include "core/Types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <random>

namespace ea::test {

using json = nlohmann::json;

class DeterministicRng {
public:
    explicit DeterministicRng(uint64_t seed = 42);
    uint64_t next();
    std::string next_string(size_t min_len = 0, size_t max_len = 1024);
    uint8_t next_byte();
    bool next_bool();
    int next_int(int min, int max);
private:
    std::mt19937_64 engine_;
};

class FuzzGenerator {
public:
    explicit FuzzGenerator(uint64_t seed = 42);

    std::string random_json(size_t max_depth = 5);
    std::string random_string(size_t min_len = 0, size_t max_len = 1024);
    std::string random_toml();
    std::string random_url();
    std::string random_http_response();
    json random_tool_call();
    std::vector<StreamChunk> random_stream_chunks(size_t count);
    std::string random_sse_stream(size_t event_count = 10);

private:
    DeterministicRng rng_;
};

}  // namespace ea::test
```

### 3.6 通用辅助函数

```cpp
// tests/common/TestHelpers.h
#pragma once
#include "core/Types.h"
#include <string>
#include <vector>

namespace ea::test {

ToolCall make_call(const std::string& tool, const std::string& id,
                   const json& args = json::object());
LLMResponse make_text(const std::string& text,
                      const std::string& stop = "stop");
LLMResponse make_tools(std::vector<ToolCall> calls);
LLMResponse make_error_response(const std::string& error_msg);

}  // namespace ea::test
```

---

## 4. 各测试类型详细设计

### 4.1 冒烟测试 `[smoke]`

**目的：** 30 秒内验证核心功能链路可用，用于快速回归。

**测试场景：**

| 场景 | 验证点 |
|------|--------|
| AgentLoop 基本对话 | Provider → AgentLoop → 输出回调 |
| 工具执行 | Provider 请求 tool → execute → Provider 再次调用 |
| 记忆存取 | InMemoryBackend store → recall |
| 安全策略默认值 | SecurityPolicy 默认为 Supervised |
| HTTP 服务器启动 | /health 端点返回 200 |
| 配置加载 | TOML 配置文件解析成功 |
| 插件加载 | MockPlugin dlopen 成功 |

### 4.2 集成测试 `[integration]`（扩展）

**新增场景：**

| 场景 | 验证点 |
|------|--------|
| 插件全生命周期 | load → init → activate → deactivate → unload 与 ToolRegistry 交互 |
| Subagent 记忆隔离 | 子 agent 不可见父 agent 的 ScopedMemory 私有记忆 |
| BudgetTracker + AgentLoop | BudgetTracker 作为 Provider 装饰器正确追踪 usage |
| Provider fallback 切换 | 主 Provider 失败后 ReliableProvider 切换到 fallback |
| 多会话并发 | SessionManager 隔离不同会话的上下文 |
| 流式 + 工具调用 | 流式响应中包含 tool_use 的正确处理 |
| 事件系统 + AgentLoop | AgentEvent 在正确时机触发 |

### 4.3 性能基准测试 `[benchmark]`

**使用 Catch2 BENCHMARK 宏，零额外依赖。**

**基准场景：**

| 场景 | 指标 | 目标 |
|------|------|------|
| AgentLoop 单轮对话 | 延迟 | <1ms (Mock) |
| AgentLoop 工具调用往返 | 延迟 | <2ms (Mock) |
| SqliteMemory store 100 条 | 吞吐 | >10K ops/s |
| SqliteMemory FTS5 recall (10K 条目) | 延迟 | <5ms |
| Tool execute (Mock) | 延迟 | <0.1ms |
| Provider 响应解析 (OpenAI 格式) | 延迟 | <0.5ms |
| 并发 10 会话 (HTTP) | 吞吐 | >100 req/s |
| 插件加载/卸载 | 延迟 | <10ms |

### 4.4 安全测试 `[security]`（4 个维度）

#### 4.4.1 策略边界测试

验证 SecurityPolicy 三个级别在各种输入下的行为：

- **Full autonomy：** 允许所有命令、路径、工具
- **Supervised：** 阻止危险命令、限制路径在工作区内、白名单机制
- **ReadOnly：** 阻止所有命令、只允许只读工具

**边界场景：**
- 命令注入（`ls; rm -rf /`、`cat $(malicious)`、`ls \`rm -rf /\``）
- 路径遍历（`../../../etc/passwd`、`/tmp/../../etc/shadow`）
- 工具权限（dangerous 工具需要审批、mutating 工具在 ReadOnly 下被阻止）
- 白名单绕历（包含注入的白名单命令、路径中的符号链接）

#### 4.4.2 插件/工具安全隔离测试

- Untrusted 插件的 PluginContext 中 host service handles 为 null
- PluginToolAdapter 在插件抛出异常时返回安全默认值
- PluginProviderAdapter 在插件崩溃时不影响主 Provider
- PluginStepAdapter 在插件步骤失败时返回 Error::plugin
- PluginListenerAdapter 吞掉异常，不影响主事件流
- FileTool 路径遍历被 SecurityPolicy 阻止
- ShellTool 命令注入被 SecurityPolicy 阻止
- WebTool URL 白名单验证

#### 4.4.3 模糊/注入测试

- **JSON 模糊：** 畸形 JSON、超深嵌套、特殊字符、无效 UTF-8
- **配置模糊：** 畸形 TOML、缺失字段、类型错误、超长值
- **Provider 响应模糊：** 畸形 LLMResponse、空 tool_calls + tool_use stop_reason、超大 content
- **SQL 注入：** Memory recall 中的 SQL 注入尝试
- **SSE 流模糊：** 畸形 SSE 事件、不完整事件、超大事件

#### 4.4.4 Sanitizer 内存安全测试

CI 中添加 ASan/UBSan 构建矩阵：

```yaml
sanitizer-tests:
  strategy:
    matrix:
      sanitizer: [address, undefined]
  steps:
    - cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }} -fno-omit-frame-pointer"
    - cmake --build . -j$(nproc)
    - ./tests/security/ea-security-tests
    - ./tests/unit/ea-unit-tests
```

### 4.5 兼容性测试 `[compat]`

#### 4.5.1 构建模式兼容性

验证三种构建模式（CLI/Embedded/Server）和功能开关组合：

- CLI 模式：所有功能可用
- Embedded 模式：streaming/web/router/fallback/MCP/plugins 被禁用
- Server 模式：router/fallback 被强制启用
- 各模式下的 `#if EA_ENABLE_*` 代码路径正确编译和运行

#### 4.5.2 编译器兼容性

CI 矩阵验证 GCC 12+ 和 Clang 16+ 下的行为一致性。

#### 4.5.3 平台兼容性

- Linux x86_64（默认 CI 环境）
- WSL2（开发环境）
- Android NDK 交叉编译（arm64-v8a, API level 21）

#### 4.5.4 Provider 协议兼容性

- OpenAI API 格式解析（chat completions + tool_calls）
- Anthropic API 格式解析（content blocks + tool_use）
- Ollama API 格式解析（native + OpenAI 兼容模式）
- 畸形/不完整响应的优雅降级

### 4.6 回归测试 `[regression]`

为历史 Bug 修复添加专门的回归测试用例，每个用例关联原始 Issue 描述：

- AgentLoop 处理空 tool_calls + tool_use stop_reason
- ScopedMemory 空 read_allowlist 不导致无限循环
- SqliteMemory WAL 模式下并发读写不死锁
- PluginLoader dlopen 失败不崩溃
- ReliableProvider fallback 切换后状态正确

### 4.7 可用性测试 `[usability]`

- `embedded-agent/Agent.h` 公共 API 可正常使用
- `embedded-agent/PluginApi.h` 插件 API 可正常实现
- 错误消息包含可操作信息（如模型名、路径、建议）
- 配置文件缺失/错误时有清晰的错误提示
- 默认配置可开箱即用

---

## 5. CI/CD 自动化管道

### 5.1 CI 作业矩阵

| 作业 | 触发条件 | 预期耗时 | 失败策略 |
|------|----------|----------|----------|
| unit-tests | 每次 push/PR | <3min | 阻塞合并 |
| integration-tests | 每次 push/PR | <2min | 阻塞合并 |
| security-tests | 每次 push/PR | <5min | 阻塞合并 |
| system-tests | PR 合并前 | <3min | 阻塞合并 |
| compat-tests | PR 合并前 | <10min | 阻塞合并 |
| bench-tests | 每日/手动 | ~5min | 仅报告 |
| coverage | 每日 | ~5min | 仅报告 |

### 5.2 CI 工作流

```yaml
name: CI
on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 6 * * *'  # 每日 UTC 6:00
  workflow_dispatch:       # 手动触发

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        build_type: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev
      - name: Configure
        run: cmake -B build -DENABLE_TESTS=ON -DEA_ENABLE_UNIT_TESTS=ON
             -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run unit tests
        run: ./build/tests/unit/ea-unit-tests --reporter compact
      - name: Run smoke tests
        run: ./build/tests/unit/ea-unit-tests "[smoke]" --reporter compact

  integration-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev
      - name: Configure
        run: cmake -B build -DENABLE_TESTS=ON -DEA_ENABLE_INTEGRATION_TESTS=ON
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run integration tests
        run: ./build/tests/integration/ea-integration-tests --reporter compact

  security-tests:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, undefined]
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev
      - name: Configure with sanitizer
        run: cmake -B build -DENABLE_TESTS=ON
             -DEA_ENABLE_SECURITY_TESTS=ON -DEA_ENABLE_UNIT_TESTS=ON
             -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }} -fno-omit-frame-pointer"
             -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run security tests
        run: ./build/tests/security/ea-security-tests --reporter compact
      - name: Run unit tests under sanitizer
        run: ./build/tests/unit/ea-unit-tests --reporter compact

  system-tests:
    runs-on: ubuntu-latest
    if: github.event_name == 'pull_request'
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev
      - name: Configure
        run: cmake -B build -DENABLE_TESTS=ON -DEA_ENABLE_SYSTEM_TESTS=ON
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run system tests
        run: ./build/tests/system/ea-system-tests --reporter compact

  compat-tests:
    runs-on: ubuntu-latest
    if: github.event_name == 'pull_request'
    strategy:
      matrix:
        build_mode:
          - name: CLI
            flags: "-DEA_MODE_CLI=ON"
          - name: EMBEDDED
            flags: "-DEA_MODE_EMBEDDED=ON"
          - name: SERVER
            flags: "-DEA_MODE_SERVER=ON"
        compiler: [g++-12, clang++-16]
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake ${{ matrix.compiler }} libsqlite3-dev
      - name: Configure
        run: cmake -B build -DENABLE_TESTS=ON ${{ matrix.build_mode.flags }}
             -DCMAKE_CXX_COMPILER=${{ matrix.compiler }}
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run tests
        run: cd build && ctest --output-on-failure

  bench-tests:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev
      - name: Configure
        run: cmake -B build -DENABLE_TESTS=ON -DEA_ENABLE_BENCH_TESTS=ON
             -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run benchmarks
        run: ./build/tests/bench/ea-bench-tests --benchmark-samples 100

  coverage:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get install -y cmake g++ libsqlite3-dev lcov
      - name: Configure with coverage
        run: cmake -B build -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
             -DCMAKE_CXX_FLAGS="--coverage -g -O0"
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Run all tests
        run: cd build && ctest --output-on-failure
      - name: Generate coverage report
        run: |
          lcov --capture --directory build --output-file coverage.info
          lcov --remove coverage.info '/usr/*' 'third_party/*' 'tests/*' --output-file coverage.info
          lcov --list coverage.info
      - name: Upload coverage
        uses: codecov/codecov-action@v4
        with:
          files: coverage.info
```

### 5.3 本地开发工作流

```bash
# 快速验证（开发时，<5s）
cmake --build build -j$(nproc) && ./build/tests/unit/ea-unit-tests "[smoke]"

# 完整单元测试
./build/tests/unit/ea-unit-tests

# 集成测试
./build/tests/integration/ea-integration-tests

# 安全测试（需要 ASan 构建）
cmake -B build-asan -DENABLE_TESTS=ON -DEA_ENABLE_SECURITY_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build-asan -j$(nproc)
./build-asan/tests/security/ea-security-tests

# 性能基准
cmake -B build-release -DENABLE_TESTS=ON -DEA_ENABLE_BENCH_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(nproc)
./build-release/tests/bench/ea-bench-tests

# 全部测试
cd build && ctest --output-on-failure -j$(nproc)
```

---

## 6. 实施优先级

### Phase 1：基础设施 + 分层迁移（最高优先级）

1. 创建 `tests/common/` 共享基础设施（MockProvider、MockTool、Fixtures、TestHelpers）
2. 创建 `tests/unit/`、`tests/integration/`、`tests/system/` 目录和 CMakeLists.txt
3. 迁移现有测试文件到对应目录，替换本地 Mock 为共享 Mock
4. 拆分 CMake 构建为 5 个独立二进制
5. 更新 CI 管道

### Phase 2：冒烟 + 回归 + 安全测试（高优先级）

1. 添加冒烟测试（`test_smoke.cpp`）
2. 添加回归测试（`test_regression.cpp`）
3. 添加安全策略边界测试（`test_policy_boundary.cpp`）
4. 添加工具隔离测试（`test_tool_isolation.cpp`）
5. 添加插件沙箱测试（`test_plugin_sandbox.cpp`）
6. 添加注入测试（`test_injection.cpp`）
7. CI 添加 ASan/UBSan 矩阵

### Phase 3：性能 + 模糊测试（中优先级）

1. 添加性能基准测试（5 个 bench 文件）
2. 添加模糊测试（JSON/Config/Provider）
3. CI 添加每日基准和覆盖率作业

### Phase 4：兼容性 + 可用性 + 系统测试（中优先级）

1. 添加构建模式兼容性测试
2. 添加 Provider 协议兼容性测试
3. 添加可用性测试
4. 添加 CLI/Server/Embedded 模式端到端测试
5. CI 添加兼容性矩阵

### Phase 5：集成测试扩展（低优先级）

1. 添加插件全生命周期集成测试
2. 添加 Subagent 记忆隔离集成测试
3. 添加 BudgetTracker + AgentLoop 集成测试
4. 添加 Provider fallback 切换集成测试
5. 添加多会话并发集成测试

---

## 7. 测试标签体系

| 标签 | 层级 | 含义 |
|------|------|------|
| `[unit]` | 单元 | 单个类/函数测试 |
| `[integration]` | 集成 | 跨模块交互测试 |
| `[system]` | 系统 | 端到端流程测试 |
| `[smoke]` | 冒烟 | 核心链路快速验证 |
| `[benchmark]` | 性能 | Catch2 BENCHMARK |
| `[security]` | 安全 | 安全策略/隔离测试 |
| `[fuzz]` | 模糊 | 模糊/注入测试 |
| `[compat]` | 兼容性 | 构建/编译器/平台兼容 |
| `[regression]` | 回归 | 历史 Bug 修复验证 |
| `[usability]` | 可用性 | API 易用性验证 |

模块标签保持现有约定：`[memory]`、`[agent]`、`[provider]`、`[tool]`、`[security]`、`[plugin]`、`[mcp]`、`[server]`、`[budget]`、`[config]`、`[conversation]`、`[subagent]`、`[streaming]`、`[compression]`、`[strategy]`、`[approval]`、`[loopdetect]`、`[http]`、`[chat]`、`[event]`

标签组合查询示例：
- `./ea-unit-tests "[smoke]"` — 只跑冒烟测试
- `./ea-unit-tests "[memory]"` — 只跑 memory 模块单元测试
- `./ea-security-tests "[fuzz]"` — 只跑模糊测试
- `./ea-integration-tests "[agent][memory]"` — 只跑 agent+memory 集成测试
- `./ea-unit-tests "~[benchmark]"` — 排除性能测试
