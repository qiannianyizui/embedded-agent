# Phase 3E: Streaming Integration — Design Spec

**Date:** 2026-07-17
**Status:** Draft
**Depends on:** Phase 2D (Agent Loop), Phase 3B (Context Compression)

## Overview

Integrate `IProvider::stream_chat()` into the AgentLoop so that LLM responses are delivered token-by-token to the caller via a new `StreamFn` callback, instead of waiting for the complete response. When the provider does not support streaming, automatically fall back to `chat()` with simulated streaming chunks.

## Goals

- Token-level streaming: each text fragment arrives immediately via callback
- Tool call events (ToolCallBegin/Delta/End) are forwarded through the same callback
- Automatic degradation: providers without streaming get `chat()` + simulated chunk sequence
- Zero impact on non-streaming path when `StreamFn` is not provided
- Subagents continue using non-streaming path (no changes required)

## Type & Interface Changes

### StreamFn Type

Defined in `AgentLoop.h`, reusing existing `StreamChunk`:

```cpp
using StreamFn = std::function<void(const StreamChunk&)>;
```

`StreamChunk` already has the necessary types: `Content`, `ToolCallBegin`, `ToolCallDelta`, `ToolCallEnd`, `Done`, `Error`.

### AgentLoop::Config Extension

```cpp
struct Config {
    int max_iterations = 90;
    int max_tool_output_bytes = 65536;
    int max_messages = 100;
    bool auto_memory = true;
    bool stream = true;  // Enable streaming output
};
```

### AgentLoop Constructor Extension

New `StreamFn` parameter after `OutputFn`:

```cpp
AgentLoop(IProvider* provider,
          ToolRegistry* registry,
          IMemory* memory,
          Config config,
          OutputFn output,
          StreamFn stream_fn = nullptr,       // NEW
          security::SecurityPolicy* policy = nullptr,
          security::IApprovalHandler* approval = nullptr,
          ContextCompressor* compressor = nullptr);
```

### TurnContext Extension

```cpp
struct TurnContext {
    // ... existing fields ...
    std::function<void(const StreamChunk&)> stream_callback;  // NEW
};
```

`AgentLoop::run()` assigns `stream_fn_` to `ctx.stream_callback` when creating each `TurnContext`.

## CallProviderStep Streaming Integration

### Core Logic

`CallProviderStep::execute()` now has three paths:

**Path 1 — Streaming (stream_callback set + provider supports streaming):**

```cpp
if (ctx.stream_callback && ctx.provider->capabilities().streaming) {
    std::string accumulated_content;
    std::vector<ToolCall> accumulated_calls;

    auto result = ctx.provider->stream_chat(
        messages, ctx.tool_specs, "",
        [&](const StreamChunk& chunk) {
            if (ctx.interrupted) throw StreamInterrupted{};
            ctx.stream_callback(chunk);

            if (chunk.type == StreamChunk::Type::Content) {
                accumulated_content += chunk.data;
            }
            if (chunk.type == StreamChunk::Type::ToolCallEnd && chunk.tool_call) {
                accumulated_calls.push_back(chunk.tool_call.value());
            }
        },
        opts
    );

    if (!result.ok()) return result.error();

    ctx.response.content = std::move(accumulated_content);
    ctx.response.tool_calls = std::move(accumulated_calls);
    ctx.response.stop_reason = accumulated_calls.empty() ? "stop" : "tool_calls";
}
```

**Path 2 — Degraded streaming (stream_callback set + provider does NOT support streaming):**

```cpp
else if (ctx.stream_callback) {
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) return response.error();

    ctx.response = response.value();

    // Simulate Content chunk
    if (!ctx.response.content.empty()) {
        StreamChunk chunk;
        chunk.type = StreamChunk::Type::Content;
        chunk.data = ctx.response.content;
        ctx.stream_callback(chunk);
    }
    // Simulate ToolCallEnd chunks
    for (const auto& tc : ctx.response.tool_calls) {
        StreamChunk chunk;
        chunk.type = StreamChunk::Type::ToolCallEnd;
        chunk.tool_call = tc;
        ctx.stream_callback(chunk);
    }
    // Simulate Done
    StreamChunk done;
    done.type = StreamChunk::Type::Done;
    ctx.stream_callback(done);
}
```

**Path 3 — Non-streaming (no stream_callback, existing behavior):**

```cpp
else {
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) return response.error();
    ctx.response = std::move(response.value());
}
```

### Key Design Points

- **Accumulate and forward in parallel:** chunks are forwarded to the callback (user sees real-time output) AND accumulated into a complete `LLMResponse` for downstream steps (`ParseResponseStep`, `ExecuteToolsStep`), which require no changes.
- **Degradation is transparent:** when the provider doesn't support streaming, `chat()` results are wrapped as a chunk sequence — the caller cannot distinguish this from real streaming.
- **Non-streaming path unchanged:** when `stream_callback` is null, the original `chat()` path runs with zero overhead.

## AgentLoop Output Logic

### Stream vs OutputFn

In streaming mode, text is already delivered via `StreamFn` chunk-by-chunk. `OutputFn` must NOT re-emit the same content:

```cpp
// In AgentLoop::run(), at the end of each iteration
if (ctx.should_stop) {
    if (!config_.stream || !stream_fn_) {
        // Non-streaming: use OutputFn for complete text
        if (output_ && !ctx.response.content.empty()) {
            output_(ctx.response.content);
        }
    }
    // Streaming: content already delivered via StreamFn, skip OutputFn
    return {};
}
```

## main.cpp Integration

### CLI Mode Streaming

```cpp
ea::agent::AgentLoop::StreamFn stream_fn;

if (cfg.agent.stream) {
    stream_fn = [](const ea::StreamChunk& chunk) {
        if (chunk.type == ea::StreamChunk::Type::Content) {
            std::cout << chunk.data << std::flush;
        } else if (chunk.type == ea::StreamChunk::Type::Done) {
            std::cout << std::endl;
        }
        // ToolCallBegin/Delta/End are silent in CLI mode
        // Server mode can forward these via WebSocket
    };
}

ea::agent::AgentLoop loop(
    provider.get(), &registry, memory.get(),
    ea::agent::AgentLoop::Config{
        cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream
    },
    [](const std::string& text) { std::cout << text << std::endl; },
    stream_fn,
    security.get(),
    approval.get(),
    compressor.get()
);
```

## Config Extension

### AgentConfig

```cpp
// In src/config/Config.h, AgentConfig struct
bool stream = true;  // Enable streaming output
```

### TOML Parsing

```toml
[agent]
stream = true
```

Parsed in `Config.cpp` after existing agent fields.

## Error Handling & Interruption

### StreamInterrupted Exception

`stream_chat()` is a blocking call. To support interruption during streaming:

```cpp
// In TurnContext.h (alongside the interrupted atomic it depends on)
struct StreamInterrupted : std::exception {
    const char* what() const noexcept override { return "stream interrupted"; }
};
```

In the `on_chunk` callback inside `CallProviderStep`:

```cpp
[&](const StreamChunk& chunk) {
    if (ctx.interrupted) throw StreamInterrupted{};
    ctx.stream_callback(chunk);
    // ... accumulation ...
}
```

In `CallProviderStep::execute()`, the streaming path wraps the call:

```cpp
try {
    auto result = ctx.provider->stream_chat(...);
    if (!result.ok()) return result.error();
} catch (const StreamInterrupted&) {
    return Error::timeout("stream interrupted");
} catch (const std::exception& e) {
    return Error::net(std::string("stream error: ") + e.what());
}
```

### Provider Exception Safety

`OpenAIProvider::parse_sse_chunk` already catches `json::exception` internally. The outer catch in `CallProviderStep` handles any uncaught exceptions from provider implementations.

## Subagent Compatibility

`Subagent::execute()` creates `AgentLoop` without `StreamFn` (remains `nullptr`). Subagents use the non-streaming path internally, collecting results via `OutputFn`. No changes required to `Subagent` or `SubagentOrchestrator`.

## Test Strategy

### Test Cases

| # | Case | Tag | Description |
|---|------|-----|-------------|
| 1 | StreamFn content forwarding | `[streaming]` | Mock provider sends Content chunks, verify callback receives them |
| 2 | Streaming accumulates LLMResponse | `[streaming]` | Verify `ctx.response` is correctly accumulated from chunks |
| 3 | Streaming tool call accumulation | `[streaming]` | ToolCallBegin → Delta → End sequence, verify `ctx.response.tool_calls` |
| 4 | Degraded streaming (chat + simulated) | `[streaming]` | Provider without streaming, verify simulated chunk sequence |
| 5 | Non-streaming path unchanged | `[streaming]` | `stream_callback` null → original `chat()` path |
| 6 | Stream interruption | `[streaming]` | Set `interrupted`, verify `StreamInterrupted` thrown and error returned |
| 7 | AgentLoop streaming skips OutputFn | `[streaming]` | Streaming mode: `OutputFn` not called |
| 8 | AgentLoop non-streaming uses OutputFn | `[streaming]` | Non-streaming mode: `OutputFn` works normally |
| 9 | Config stream field parsing | `[streaming]` | TOML `stream = false` parsed correctly |

### Mock Provider

New `MockStreamProvider` in test file:

- `capabilities().streaming = true`
- `stream_chat()` sends a preset sequence of chunks
- `chat()` also implemented (for degradation tests)

### Test File

New file: `tests/test_streaming.cpp`, tagged `[streaming]`.

## Files Changed

| File | Change |
|------|--------|
| `src/agent/AgentLoop.h` | Add `StreamFn`, `stream_fn_` member, `Config::stream` |
| `src/agent/AgentLoop.cpp` | Pass `stream_fn_` to TurnContext, conditional OutputFn |
| `src/agent/TurnContext.h` | Add `stream_callback` field, `StreamInterrupted` |
| `src/agent/steps/CallProviderStep.cpp` | Three-path streaming logic |
| `src/agent/steps/CallProviderStep.h` | No change needed (logic is in .cpp) |
| `src/config/Config.h` | `AgentConfig::stream` field |
| `src/config/Config.cpp` | Parse `agent.stream` |
| `src/main.cpp` | Create `StreamFn`, pass to AgentLoop |
| `tests/test_streaming.cpp` | New: 9 test cases |
| `tests/CMakeLists.txt` | Add `test_streaming.cpp` |
