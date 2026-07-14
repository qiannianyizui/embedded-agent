# Embedded Agent C++ 骨架设计

> 日期: 2026-07-14
> 状态: 待审阅

## 1. 项目定位

轻量级 C++ AI Agent 运行时，支持 Linux + Android，聚焦嵌入式/移动/边缘场景。

核心差异化：二进制 <5MB，启动 <30ms，内存 <15MB，NDK 原生编译。

参照项目：ZeroClaw (Rust) + Hermes (Python)，取其架构精华，用 C++ 重实现。

## 2. 技术决策

| 项 | 选择 | 理由 |
|----|------|------|
| C++ 标准 | C++17 | NDK r21+ 完整支持，有 optional/variant/string_view |
| 异步模型 | 同步 + std::thread | 简单可靠，Agent 并发量低，Android 完美兼容 |
| 构建系统 | CMake | NDK 官方 toolchain 基于 CMake |
| HTTP 客户端 | cpp-httplib | header-only，零系统依赖，交叉编译无痛 |
| TLS | mbedTLS | 体积小(~200KB)，cpp-httplib 原生支持，统一 Linux+Android |
| JSON | nlohmann/json | header-only，C++17 原生支持 |
| 配置解析 | toml11 | header-only，TOML 格式 |
| 日志 | spdlog | header-only 可选，性能极高 |
| CLI | CLI11 | header-only |
| 数据库 | SQLite amalgamation + 原生 C API | 自封装 RAII，完全可控 FTS5/WAL |
| 错误处理 | Result\<T\> | 无异常开销，强制错误处理 |
| SSE 解析 | 自写 | ~100-200 行，零依赖 |
| 测试 | Catch2 | header-only，BDD 风格 |
| 命名空间 | `ea` | 简短，避免冲突 |
| Release 禁用 | 异常、RTTI | Result\<T\> 不用异常，体积减少 15-20% |

## 3. 架构

### 3.1 模块依赖关系

```
                          ┌─────────┐
                          │  main   │  (CLI 入口)
                          └────┬────┘
                               │
                          ┌────▼────┐
                          │  agent  │  (AgentLoop, SystemPrompt)
                          └────┬────┘
                               │
               ┌───────────────┼───────────────┐
               │               │               │
          ┌────▼────┐    ┌────▼────┐    ┌─────▼─────┐
          │ provider │    │  tool   │    │  memory   │
          └────┬────┘    └────┬────┘    └─────┬─────┘
               │               │               │
               └───────────────┼───────────────┘
                               │
                          ┌────▼────┐
                          │  core   │  (纯虚接口 + Types, INTERFACE 库)
                          └────┬────┘
                               │
          ┌────────────┬───────┼────────┬────────────┐
          │            │       │        │            │
     ┌────▼───┐  ┌────▼──┐ ┌─▼──┐ ┌───▼───┐  ┌────▼───┐
     │ config │  │security│ │plat│ │  net  │  │   io   │
     └────────┘  └───────┘ │form│ │(http/ │  │(fs/proc│
                           └────┘ │tls/ws)│  │/logger)│
                                  └───────┘  └────────┘
                               │               │
                          ┌────▼───────────────▼────┐
                          │        base             │
                          │  (Result, Error, String, │
                          │   JsonHelper)            │
                          └─────────────────────────┘
```

**依赖规则**：
- 单向依赖：上层依赖下层，下层不知道上层存在
- core 是分界线：只有纯虚类和类型定义，零实现依赖
- common 三个子目录：base(纯逻辑) → io(本地I/O) / net(网络I/O)；io 和 net 都依赖 base，但互相不依赖
- provider/tool/memory 只依赖 core + common，不互相依赖
- agent 依赖 core 的接口（通过指针/引用），不直接依赖具体实现

### 3.2 目录结构

```
embedded-agent/
├── CMakeLists.txt
├── cmake/
│   └── Toolchain-Android.cmake
├── src/
│   ├── common/
│   │   ├── base/
│   │   │   ├── Result.h
│   │   │   ├── Error.h
│   │   │   ├── StringUtil.h
│   │   │   └── JsonHelper.h
│   │   ├── io/
│   │   │   ├── Logger.h/.cpp
│   │   │   ├── FileSystem.h/.cpp
│   │   │   └── Process.h/.cpp
│   │   ├── net/
│   │   │   ├── HttpClient.h/.cpp
│   │   │   ├── TlsConfig.h/.cpp
│   │   │   ├── SseParser.h/.cpp
│   │   │   ├── RetryPolicy.h/.cpp
│   │   │   └── WebSocket.h/.cpp      (Phase 2)
│   │   └── CMakeLists.txt
│   ├── core/
│   │   ├── Types.h
│   │   ├── IProvider.h
│   │   ├── ITool.h
│   │   ├── IMemory.h
│   │   └── IChannel.h               (Phase 3 预留)
│   ├── provider/
│   │   ├── OpenAIProvider.h/.cpp
│   │   ├── OllamaProvider.h/.cpp     (Phase 2 实现)
│   │   ├── ProviderFactory.h/.cpp
│   │   └── CMakeLists.txt
│   ├── tool/
│   │   ├── ToolRegistry.h/.cpp
│   │   ├── ShellTool.h/.cpp
│   │   ├── FileTool.h/.cpp
│   │   ├── SearchTool.h/.cpp
│   │   ├── WebTool.h/.cpp
│   │   ├── MemoryTool.h/.cpp
│   │   └── CMakeLists.txt
│   ├── memory/
│   │   ├── SqliteMemory.h/.cpp
│   │   └── CMakeLists.txt
│   ├── agent/
│   │   ├── AgentLoop.h/.cpp
│   │   ├── SystemPrompt.h/.cpp
│   │   └── CMakeLists.txt
│   ├── platform/
│   │   ├── Platform.h/.cpp
│   │   └── CMakeLists.txt
│   ├── config/
│   │   ├── Config.h/.cpp
│   │   └── CMakeLists.txt
│   ├── security/
│   │   ├── SecurityPolicy.h/.cpp
│   │   └── CMakeLists.txt
│   └── main.cpp
├── include/
│   └── embedded-agent/
│       ├── Agent.h                   (对外统一 API)
│       └── Version.h
├── tests/
│   ├── CMakeLists.txt
│   ├── test_common_result.cpp
│   ├── test_common_string.cpp
│   ├── test_common_json.cpp
│   ├── test_common_sse.cpp
│   ├── test_provider_openai.cpp
│   ├── test_tool_registry.cpp
│   ├── test_tool_shell.cpp
│   ├── test_tool_file.cpp
│   ├── test_memory_sqlite.cpp
│   ├── test_agent_loop.cpp
│   └── test_config.cpp
├── configs/
│   └── default.toml
└── third_party/                      (FetchContent 下载，不提交 git)
```

## 4. 核心接口

### 4.1 Types.h

命名空间 `ea`，定义：Role, Message, ToolCall, Usage, LLMResponse, StreamChunk, ToolSpec, ToolResult, MemoryEntry, ChatOptions。

详见设计方案第 2 部分。

### 4.2 IProvider

```cpp
class IProvider {
public:
    virtual ~IProvider() = default;
    virtual std::string name() const = 0;
    virtual std::vector<std::string> list_models() const = 0;
    virtual Result<LLMResponse> chat(...) = 0;
    virtual Result<void> stream_chat(...) = 0;
};
```

### 4.3 ITool

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual Result<ToolResult> execute(const json& args) = 0;
};
```

### 4.4 IMemory

```cpp
class IMemory {
public:
    virtual ~IMemory() = default;
    virtual Result<std::string> store(content, category, importance) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(query, limit) = 0;
    virtual Result<bool> forget(id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(limit, offset) = 0;
    virtual Result<int> count() = 0;
};
```

### 4.5 IChannel (Phase 3 预留)

```cpp
class IChannel {
public:
    virtual ~IChannel() = default;
    virtual std::string name() const = 0;
    virtual Result<void> send(const std::string& message) = 0;
};
```

## 5. common 模块

### 5.1 base/

- **Result.h**: Result\<T\> (variant\<T, Error\>) + Result\<void\> 特化
- **Error.h**: ErrorCode 枚举 + Error 结构体 + 静态工厂方法
- **StringUtil.h**: trim, split, starts_with, ends_with, to_lower, replace_all
- **JsonHelper.h**: get_or\<T\>, get_path (嵌套路径访问)

### 5.2 io/

- **Logger.h/.cpp**: spdlog 封装，EA_DEBUG/INFO/WARN/ERROR 宏
- **FileSystem.h/.cpp**: POSIX API 文件操作，home_dir/config_dir/data_dir/mkdir_p/read_file/write_file
- **Process.h/.cpp**: posix_spawn + pipe + poll，带超时的命令执行

### 5.3 net/

- **HttpClient.h/.cpp**: cpp-httplib 封装，get/post/stream_get/stream_post
- **TlsConfig.h/.cpp**: mbedTLS 配置，CA 证书自动检测
- **SseParser.h/.cpp**: 增量式 SSE 解析器，feed() + 回调
- **RetryPolicy.h/.cpp**: 指数退避重试策略
- **WebSocket.h/.cpp**: Phase 2 实现

## 6. 业务模块

### 6.1 provider/

- **OpenAIProvider**: OpenAI 兼容协议，覆盖 DeepSeek/Qwen/Ollama/Together/Groq 等
- **OllamaProvider**: Phase 2 实现，本地推理
- **ProviderFactory**: 根据配置创建 Provider 实例

### 6.2 tool/

- **ToolRegistry**: 注册 + 查找 + 执行 + get_all_specs
- **ShellTool**: shell 命令执行，supervised 模式限制危险命令
- **FileTool**: 合并 read/write/edit，用 action 字段区分
- **SearchTool**: ripgrep 优先，grep 降级
- **WebTool**: 合并 search/fetch，用 action 字段区分
- **MemoryTool**: 合并 store/recall/forget，用 action 字段区分

### 6.3 memory/

- **SqliteMemory**: SQLite + FTS5 + WAL，表结构 memories + memories_fts

### 6.4 agent/

- **AgentLoop**: 核心对话循环，max_iterations 预算，Tool 错误回传 LLM
- **SystemPrompt**: 三层构建 (stable + context + volatile)

### 6.5 platform/

- **Platform**: 检测 Linux/Android/WSL，路径管理，能力查询

### 6.6 config/

- **Config**: TOML 加载，默认值 → 文件 → 环境变量覆盖

### 6.7 security/

- **SecurityPolicy**: AutonomyLevel (ReadOnly/Supervised/Full)，命令过滤，工作区边界

## 7. 数据流

用户输入 → AgentLoop::run() → build_messages() (system prompt + 记忆 + 历史) → provider_->chat() → LLM 返回 tool_calls → registry_.execute() → Tool 执行 → 结果回传 LLM → LLM 最终回复 → output_() 给用户。

详见设计方案第 6 部分完整流程图。

## 8. 错误处理

| 层级 | 策略 |
|------|------|
| HttpClient | 网络错误 → Result::err(NetworkError)；429 → RetryPolicy 自动重试；401 → Result::err(AuthError) |
| OpenAIProvider | 解析错误 → Result::err(ParseError)；HTTP 错误透传 |
| ToolRegistry | tool 不存在 → Result::err(NotFound)；执行失败透传 |
| ShellTool | 安全拦截 → Result::err(SecurityBlocked)；超时 → Result::err(Timeout)；exit_code!=0 → ToolResult{is_error=true} |
| SqliteMemory | SQLite API 错误 → Result::err(DbError)；文件不存在 → 自动 create_tables() |
| AgentLoop | Provider 错误 → 终止循环，告知用户；Tool 错误 → 回传 LLM 让其修正；迭代耗尽 → 警告 |

**关键原则**：Tool 错误不终止循环（回传 LLM 修正），Provider 错误终止循环（LLM 不可用）。

## 9. 测试策略

- Mock Provider/Tool 用于 AgentLoop 和 ToolRegistry 测试
- SQLite :memory: 数据库用于 SqliteMemory 测试
- SseParser 用预构造的分片数据测试增量解析
- OpenAIProvider 用 mock HttpClient 测试请求构建和响应解析

## 10. 构建系统

- 各模块编译为 OBJECT 库，合并为 libembedded-agent-core.a
- core 是 INTERFACE 库（纯头文件）
- SQLite amalgamation 手动下载（file(DOWNLOAD) + execute_process）
- 其他依赖 FetchContent 自动下载
- Release 模式禁用异常和 RTTI
- Android 交叉编译通过独立 Toolchain-Android.cmake

## 11. 分阶段实施

### Phase 1 — MVP

- IProvider + OpenAIProvider
- ITool + ToolRegistry + ShellTool + FileTool + SearchTool + WebTool + MemoryTool
- IMemory + SqliteMemory (FTS5)
- AgentLoop + SystemPrompt
- Platform 检测 + 路径管理
- Config (TOML)
- CLI 入口
- SecurityPolicy (骨架)

### Phase 2 — 增强

- Anthropic/Gemini 原生 Provider
- Ollama 本地推理 Provider
- 流式输出 + 流式 Tool Calling
- 上下文自动压缩
- Memory 向量搜索 (embedding)
- MCP 客户端
- 安全策略集成到 AgentLoop
- Cron 定时任务
- WebSocket (Gateway 用)

### Phase 3 — 生态

- 30+ 消息渠道
- HTTP/WebSocket Gateway
- 子 Agent / 多 Agent 协作
- SOP 自动化引擎
- WASM 插件系统
- Skills 系统
- 硬件外设 (USB, GPIO, Serial)
- TUI 界面
