# embedded-agent

**C++17 轻量级 AI Agent 框架** — 面向 Linux 与嵌入式场景，当前以 TUI 交互为主，核心以静态库形式供嵌入式集成。

## 特性

- **分层微内核架构** — 核心接口 + 可插拔策略/装饰器，运行时 TUI 模式
- **多 Provider 支持** — OpenAI、Anthropic、Ollama，内置重试/降级/路由
- **可扩展工具系统** — Toolset 分组 + 条件可用性 + 安全元数据
- **持久化记忆** — SQLite FTS5 全文检索 + InMemory/Null 后端 + Agent 隔离
- **循环检测** — 精确重复 / 乒乓交替 / 无进展 三模式自动检测与升级干预
- **TurnStep 链** — 可插拔步骤编排，支持自定义步骤注入
- **依赖最少** — 仅需 CMake 3.16+、C++17 编译器；第三方库构建时自动拉取

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
- Git（安装插件/注册 marketplace 需要）
- 首次构建需要网络（FetchContent 拉取第三方库）

### 构建

```bash
git clone https://gitee.com/QianNianYiZui_admin_admin/embedded-agent.git
cd embedded-agent
mkdir build && cd build

# 默认 TUI 模式
cmake ..
cmake --build . -j$(nproc)

# 产物为 ./ea；核心逻辑同时编译为静态库 embedded-agent-core
```

### 运行

```bash
# 首次运行会生成默认配置 ~/.embedded-agent/config.toml
./ea

# 指定配置文件
./ea -c /path/to/config.toml

# 调试模式
./ea --debug
```

交互式输入，`/quit` 或 `/exit` 退出。

### 配置

配置文件 `~/.embedded-agent/config.toml`：

```toml
[provider]
type = "openai_compatible"
base_url = "https://api.openai.com/v1"
api_key = "sk-..."
default_model = "gpt-4o"

[agent]
max_iterations = 90

[security.approval]
mode = "auto"
auto_approve_dangerous = false

[skills]
enable = true
plugin_mirrors = []   # 例如 ["https://ghfast.top/"]
```

Provider 类型：`openai_compatible` / `anthropic` / `ollama`

## 模块说明

| 模块 | 路径 | 说明 |
|------|------|------|
| **core** | `src/core/` | 核心接口：IProvider, ITool, IMemory, Types |
| **agent** | `src/agent/` | AgentLoop, TurnStep 链, LoopDetector, SystemPrompt |
| **provider** | `src/provider/` | OpenAI/Anthropic/Ollama 实现, ReliableProvider, RouterProvider |
| **tool** | `src/tool/` | ToolRegistry, Toolset, Shell/File/Search/Web/Memory 工具 |
| **skill** | `src/skill/` | SKILL.md 发现/解析，skills_list / skill_view 工具 |
| **memory** | `src/memory/` | SqliteMemory, InMemoryBackend, ScopedMemory, MemoryManager |
| **security** | `src/security/` | SecurityPolicy（工作区限制、命令白名单） |
| **config** | `src/config/` | TOML 配置加载 |
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
| WebTool | 抓取网页并转换为 text/markdown | ❌ | ❌ |
| MemoryTool | 记忆存取 | ✅ | ❌ |
| SkillsListTool | 按分类/关键词列出技能 | ❌ | ❌ |
| SkillViewTool | 加载技能完整内容 | ❌ | ❌ |
| SkillManageTool | 创建/更新/删除/启用/禁用技能 | ✅ | ✅ |
| PluginInstallTool | 安装插件（git URL 或 marketplace 名称） | ✅ | ✅ |
| PluginMarketplaceTool | 注册/列出/移除 marketplace | ✅ | ✅ |

- **Toolset** 分组 + `CheckFn` 条件可用性
- **ToolOutputConfig** 全局 + 单工具输出截断
- **ToolRegistry** 激活/停用 Toolset，向后兼容 `register_tool()`

## Skills 系统

技能采用 Hermes 风格的组织方式：每个技能是 `skills/<分类>/<技能名>/SKILL.md`，文件头用 YAML frontmatter 声明 `name`、`description`、`version`、`platforms`、`tags`。

- 发现目录：`~/.embedded-agent/skills`、当前目录 `./skills`、`[skills].dirs` 额外目录
- 系统提示词只注入 `<available_skills>` 索引，模型需要时通过 `skill_view` 按需加载完整内容
- `skill_manage` 支持 `create` / `update` / `delete` / `disable` / `enable`
- TUI 支持 `/skills`（列出技能）和 `/skill <name>`（加载技能）
- 技能索引按文件 mtime/size 缓存，外部修改自动失效
- 模板预处理：`${EA_SKILL_DIR}` / `${EA_SKILL_NAME}` 默认替换；`!`cmd`` 内联 shell 需开启 `[skills] inline_shell = true`
- 配置：`[skills] enable = true`，`disabled = ["技能名"]`
- 环境变量：`EA_SKILLS_DIR` 可追加技能根目录

## Plugin 系统

支持类 Claude Code 的 plugin/marketplace 机制：

- 注册 marketplace：`/plugin marketplace add <owner/repo>`
- 安装插件：`/plugin install <name>@<marketplace>` 或 `/plugin install <git-url>`
- 插件克隆到 `~/.embedded-agent/plugins/<name>`，其中的 `skills/` 目录重启后生效
- `[skills] plugin_mirrors` 可配置多个镜像源，安装时按顺序探测（`git ls-remote`）并在失败时自动切换
- agent 侧对应工具：`plugin_install`、`plugin_marketplace`

## Web 搜索后端

`web` 工具的 `search` 动作支持可插拔后端，通过 `[web]` 配置切换：

```toml
[web]
search_backend = "duckduckgo"   # duckduckgo | searxng | exa | parallel
searxng_url = ""                # 自托管 SearXNG 地址，例如 http://localhost:8080
exa_api_key = ""                # Exa 可选 API key
parallel_api_key = ""           # Parallel 可选 API key
```

- `duckduckgo`：默认，免 key，抓取 HTML 搜索页
- `searxng`：免 key，需要 `searxng_url`
- `exa` / `parallel`：opencode 同款 MCP 公共端点，默认免 key，可选配置 key 提高配额

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

当前：**455 测试用例 / 4607 断言**

## 技术栈

| 库 | 用途 | 方式 |
|----|------|------|
| nlohmann/json | JSON 处理 | 源码内附 |
| spdlog | 日志 | 源码内附 |
| CLI11 | 命令行解析 | 源码内附 |
| cpp-httplib | HTTP 客户端（网络层） | header-only（TLS 走系统 OpenSSL） |
| mbedtls | SHA-256（HRR 向量） | 源码内附 |
| SQLite3 | 嵌入式数据库 | 构建时下载 amalgamation |
| Catch2 | 测试框架 | CMake FetchContent |

## 运行模式

当前只保留 TUI 模式（FTXUI 界面）：

```bash
./ea          # 默认启动 TUI
./ea tui      # 显式指定 TUI
./ea -c path/to/config.toml
```

CLI（REPL）与 Server（HTTP API）模式已移除；如需嵌入式集成，链接
`embedded-agent-core` 静态库即可。

## 许可证

MIT License
