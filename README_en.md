# embedded-agent

**A lightweight C++17 AI Agent framework** — designed for Linux and embedded scenarios, supporting CLI / Embedded / Server runtime modes.

## Features

- **Layered Microkernel Architecture** — Core interfaces + pluggable strategies/decorators, compile-time mode switching
- **Multi-Provider Support** — OpenAI, Anthropic, Ollama with built-in retry/fallback/routing
- **Extensible Tool System** — Toolset grouping + conditional availability + security metadata
- **Persistent Memory** — SQLite FTS5 full-text search + InMemory/Null backends + Agent isolation
- **Loop Detection** — Exact repeat / ping-pong / no-progress — three-pattern auto-detection with escalating intervention
- **TurnStep Chain** — Pluggable step orchestration, supports custom step injection
- **Zero-Dependency Build** — Only requires CMake 3.16+ and a C++17 compiler; third-party libs bundled

## Architecture Overview

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

## Quick Start

### Prerequisites

- CMake ≥ 3.16
- C++17 compiler (GCC 9+ / Clang 10+)
- SQLite3 development library

### Build

```bash
git clone https://gitee.com/QianNianYiZui_admin_admin/embedded-agent.git
cd embedded-agent
mkdir build && cd build

# Default CLI mode
cmake ..
cmake --build . -j$(nproc)

# Embedded mode (no CLI interaction)
cmake .. -DEA_MODE=embedded
cmake --build . -j$(nproc)
```

### Run

```bash
# First run generates default config at ~/.embedded-agent/config.json
./embedded-agent

# Specify config file
./embedded-agent -c /path/to/config.json

# Debug mode
./embedded-agent --debug
```

Interactive input; type `/quit` or `/exit` to leave.

### Configuration

Config file `~/.embedded-agent/config.json`:

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

Provider types: `openai` / `anthropic` / `ollama`

## Modules

| Module | Path | Description |
|--------|------|-------------|
| **core** | `src/core/` | Core interfaces: IProvider, ITool, IMemory, Types |
| **agent** | `src/agent/` | AgentLoop, TurnStep chain, LoopDetector, SystemPrompt |
| **provider** | `src/provider/` | OpenAI/Anthropic/Ollama, ReliableProvider, RouterProvider |
| **tool** | `src/tool/` | ToolRegistry, Toolset, Shell/File/Search/Web/Memory tools |
| **memory** | `src/memory/` | SqliteMemory, InMemoryBackend, ScopedMemory, MemoryManager |
| **security** | `src/security/` | SecurityPolicy (workspace restriction, command allowlist) |
| **config** | `src/config/` | JSON config loading |
| **common** | `src/common/` | Result, Error, HttpClient, Logger, SSE parser |

## Provider Enhancements

| Component | Function |
|-----------|----------|
| `ProviderCapabilities` | Declare native tool calling / streaming / vision / caching capabilities |
| `ReliableProvider` | Decorator: exponential backoff retry + fallback |
| `RouterProvider` | Route to different models/providers via route_hint |
| `CredentialPool` | API key rotation + health tracking |
| `ErrorClassifier` | Error classification (retryable / non-retryable / rate-limit) |
| `PromptGuidedTools` | Inject tool descriptions for providers without native tool calling |

## Tool System

| Tool | Description | `is_mutating` | `is_dangerous` |
|------|-------------|:---:|:---:|
| ShellTool | Execute shell commands | ✅ | ✅ |
| FileTool | Read/write files | ✅ | ✅ |
| SearchTool | Search file contents | ❌ | ❌ |
| WebTool | HTTP requests | ❌ | ❌ |
| MemoryTool | Memory store/recall | ❌ | ❌ |

- **Toolset** grouping + `CheckFn` conditional availability
- **ToolOutputConfig** global + per-tool output truncation
- **ToolRegistry** activate/deactivate Toolsets, backward-compatible `register_tool()`

## Memory System

| Component | Function |
|-----------|----------|
| `SqliteMemory` | SQLite + FTS5 full-text search, WAL mode |
| `InMemoryBackend` | In-memory backend for testing / ephemeral use |
| `NullMemory` | No-op backend |
| `ScopedMemory` | Agent isolation decorator + read_allowlist for cross-agent reads |
| `MemoryManager` | Orchestration: prefetch / sync_turn / system_prompt_block |
| `MemoryFactory` | Create backends by type |

## Agent Loop

TurnStep chain execution order:

```
HistoryPrune → BuildToolSpecs → CallProvider → ParseResponse
    → ExecuteTools → LoopDetect → CollectResults
```

Loop detection — three patterns:

| Pattern | Trigger | Action |
|---------|---------|--------|
| Exact repeat | Same tool+args 3+ times consecutively | Block (clear pending calls) |
| Ping-pong | Two tools alternating for 4+ cycles | Warn (warning message) |
| No progress | Same tool with identical result 5+ times | Break (terminate loop) |

## Testing

```bash
cd build
cmake .. -DBUILD_TESTING=ON
cmake --build . -j$(nproc)

# Run all tests
./tests/ea-tests

# Filter by tag
./tests/ea-tests "[memory]"
./tests/ea-tests "[agent]"
./tests/ea-tests "[loopdetect]"
```

Current: **228 test cases / 551 assertions**

## Tech Stack

| Library | Purpose | Method |
|---------|---------|--------|
| nlohmann/json | JSON handling | Bundled |
| spdlog | Logging | Bundled |
| CLI11 | CLI parsing | Bundled |
| cpp-httplib | HTTP server | Bundled |
| mbedtls | TLS | Bundled |
| SQLite3 | Embedded database | System dependency |
| Catch2 | Test framework | CMake FetchContent |

## Compile-Time Modes

Switch runtime mode via CMake options:

```bash
# CLI mode (default) — interactive command line
cmake .. -DEA_MODE=cli

# Embedded mode — embed as a library in other applications
cmake .. -DEA_MODE=embedded

# Server mode — HTTP API service
cmake .. -DEA_MODE=server
```

Optional feature toggles:

```bash
-DEA_ENABLE_MEMORY=ON       # Memory system (default ON)
-DEA_ENABLE_STREAMING=ON    # Streaming response (default ON)
-DEA_ENABLE_TOOLS_WEB=ON    # WebTool (default ON)
-DEA_ENABLE_TOOLS_SHELL=ON  # ShellTool (default ON)
-DEA_ENABLE_ROUTER=OFF      # RouterProvider (default OFF)
-DEA_ENABLE_FALLBACK=OFF    # ReliableProvider fallback (default OFF)
```

## License

MIT License
