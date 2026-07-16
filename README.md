# embedded-agent

**C++17 轻量级 AI Agent 框架** — 面向 Linux 与嵌入式场景，支持 CLI / Embedded / Server 三种运行模式。

## 特性

- **分层微内核架构** — 核心接口 + 可插拔策略/装饰器，编译时模式切换
- **多 Provider 支持** — OpenAI、Anthropic、Ollama，内置重试/降级/路由
- **可扩展工具系统** — Toolset 分组 + 条件可用性 + 安全元数据
- **持久化记忆** — SQLite FTS5 全文检索 + InMemory/Null 后端 + Agent 隔离
- **循环检测** — 精确重复 / 乒乓交替 / 无进展 三模式自动检测与升级干预
- **TurnStep 链** — 可插拔步骤编排，支持自定义步骤注入
- **零依赖编译** — 仅需 CMake 3.16+、C++17 编译器，第三方库源码内附

## 架构概览

```
┌─────────────────────────────────────────────┐
│                  main.cpp                    │
├─────────────────────────────────────────────┤
│  AgentLoop (ITurnStep chain orchestration)   │
│  ┌─────────┬──────────┬──────────┬────────┐ │
│  │ History │ BuildTool│  Call    │ Parse  │ │
│  │ Prune   │ Specs    │ Provider │Response│ │
│  ├─────────┴──────────┴──────────┴────────┤ │
│  │ Execute │ Loop    │ Collect            │ │
│  │ Tools   │ Detect  │ Results            │ │
│  └─────────┴─────────┴────────────────────┘ │
├──────────┬──────────┬──────────┬────────────┤
│ IProvider│  ITool   │ IMemory  │ Security   │
├──────────┴──────────┴──────────┴────────────┤
│  common (Result, Error, HttpClient, Logger) │
└─────────────────────────────────────────────┘
```

## 快速开始

### 依赖

- CMake ≥ 3.16
- C++17 编译器 (GCC 9+ / Clang 10+)
- SQLite3 开发库

### 构建

```bash
git clone https://gitee.com/QianNianYiZui_admin_admin/embedded-agent.git
cd embedded-agent
mkdir build && cd build

# 默认 CLI 模式
cmake ..
cmake --build . -j$(nproc)

# 嵌入式模式（无 CLI 交互）
cmake .. -DEA_MODE=embedded
cmake --build . -j$(nproc)
```

### 运行

```bash
# 首次运行会生成默认配置 ~/.embedded-agent/config.json
./embedded-agent

# 指定配置文件
./embedded-agent -c /path/to/config.json

# 调试模式
./embedded-agent --debug
```

交互式输入，`/quit` 或 `/exit` 退出。

### 配置

配置文件 `~/.embedded-agent/config.json`：

```json
{
  "provider": {
    "type": "openai",
    "api_key": "sk-...",
    "model": "gpt-4o",
    "base_url": "https://api.openai.com/v1"
  },
  "memory": {
    "path": "",
    "enable_fts5": true
  },
  "agent": {
    "max_iterations": 90
  },
  "security": {
    "workspace": "",
    "allowed_commands": []
  }
}
```

Provider 类型：`openai` / `anthropic` / `ollama`

## 模块说明

| 模块 | 路径 | 说明 |
|------|------|------|
| **core** | `src/core/` | 核心接口：IProvider, ITool, IMemory, Types |
| **agent** | `src/agent/` | AgentLoop, TurnStep 链, LoopDetector, SystemPrompt |
| **provider** | `src/provider/` | OpenAI/Anthropic/Ollama 实现, ReliableProvider, RouterProvider |
| **tool** | `src/tool/` | ToolRegistry, Toolset, Shell/File/Search/Web/Memory 工具 |
| **memory** | `src/memory/` | SqliteMemory, InMemoryBackend, ScopedMemory, MemoryManager |
| **security** | `src/security/` | SecurityPolicy（工作区限制、命令白名单） |
| **config** | `src/config/` | JSON 配置加载 |
| **common** | `src/common/` | Result, Error, HttpClient, Logger, SSE 解析 |

## Provider 增强

| 组件 | 功能 |
|------|------|
| `ProviderCapabilities` | 声明原生工具调用/流式/视觉/缓存等能力 |
| `ReliableProvider` | 装饰器：指数退避重试 + 降级回退 |
| `RouterProvider` | 按 route_hint 路由到不同模型/Provider |
| `CredentialPool` | API Key 轮转 + 健康追踪 |
| `ErrorClassifier` | 错误分类（可重试/不可重试/限流） |
| `PromptGuidedTools` | 为无原生工具调用的 Provider 注入工具描述 |

## 工具系统

| 工具 | 说明 | `is_mutating` | `is_dangerous` |
|------|------|:---:|:---:|
| ShellTool | 执行 shell 命令 | ✅ | ✅ |
| FileTool | 读写文件 | ✅ | ✅ |
| SearchTool | 搜索文件内容 | ❌ | ❌ |
| WebTool | HTTP 请求 | ❌ | ❌ |
| MemoryTool | 记忆存取 | ✅ | ❌ |

- **Toolset** 分组 + `CheckFn` 条件可用性
- **ToolOutputConfig** 全局 + 单工具输出截断
- **ToolRegistry** 激活/停用 Toolset，向后兼容 `register_tool()`

## 记忆系统

| 组件 | 功能 |
|------|------|
| `SqliteMemory` | SQLite + FTS5 全文检索，WAL 模式 |
| `InMemoryBackend` | 内存后端，测试/临时场景 |
| `NullMemory` | 空操作后端 |
| `ScopedMemory` | Agent 隔离装饰器 + read_allowlist 跨 Agent 读取 |
| `MemoryManager` | 编排层：prefetch/sync_turn/system_prompt_block |
| `MemoryFactory` | 按类型创建后端 |

## Agent Loop

TurnStep 链执行顺序：

```
HistoryPrune → BuildToolSpecs → CallProvider → ParseResponse
    → ExecuteTools → LoopDetect → CollectResults
```

循环检测三模式：

| 模式 | 触发条件 | 动作 |
|------|---------|------|
| 精确重复 | 相同 tool+args 连续 3+ 次 | Block（清除待执行调用） |
| 乒乓交替 | 两个工具交替 4+ 轮 | Warn（警告提示） |
| 无进展 | 同工具同结果 5+ 次 | Break（终止循环） |

## 测试

```bash
cd build
cmake .. -DBUILD_TESTING=ON
cmake --build . -j$(nproc)

# 运行全部测试
./tests/ea-tests

# 按标签过滤
./tests/ea-tests "[memory]"
./tests/ea-tests "[agent]"
./tests/ea-tests "[loopdetect]"
```

当前：**228 测试用例 / 551 断言**

## 技术栈

| 库 | 用途 | 方式 |
|----|------|------|
| nlohmann/json | JSON 处理 | 源码内附 |
| spdlog | 日志 | 源码内附 |
| CLI11 | 命令行解析 | 源码内附 |
| cpp-httplib | HTTP 服务 | 源码内附 |
| mbedtls | TLS | 源码内附 |
| SQLite3 | 嵌入式数据库 | 系统依赖 |
| Catch2 | 测试框架 | CMake FetchContent |

## 编译时模式

通过 CMake 选项切换运行模式：

```bash
# CLI 模式（默认）— 交互式命令行
cmake .. -DEA_MODE=cli

# 嵌入式模式 — 作为库嵌入其他应用
cmake .. -DEA_MODE=embedded

# 服务器模式 — HTTP API 服务
cmake .. -DEA_MODE=server
```

可选功能开关：

```bash
-DEA_ENABLE_MEMORY=ON       # 记忆系统（默认 ON）
-DEA_ENABLE_STREAMING=ON    # 流式响应（默认 ON）
-DEA_ENABLE_TOOLS_WEB=ON    # WebTool（默认 ON）
-DEA_ENABLE_TOOLS_SHELL=ON  # ShellTool（默认 ON）
-DEA_ENABLE_ROUTER=OFF      # RouterProvider（默认 OFF）
-DEA_ENABLE_FALLBACK=OFF    # ReliableProvider 降级（默认 OFF）
```

## 许可证

MIT License
