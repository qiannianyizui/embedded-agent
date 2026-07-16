# Phase 3B: LLM Context Compression Design

**Date:** 2026-07-17
**Status:** Approved
**Scope:** LLM-based context compression for long conversations

## Overview

Add a `ContextCompressor` that uses LLM to summarize middle conversation turns when token count exceeds a threshold. The compressor preserves the system prompt (head) and recent N turns (tail), replacing the middle with a generated summary. Compression is triggered in `CallProviderStep` before each LLM call, operating on a copy of messages — the original history is never modified.

**Design approach:** Estimator + LLM summarizer + graceful fallback. Token count is estimated with a simple heuristic (4 chars ≈ 1 token). When over threshold, middle messages are sent to the LLM for summarization. If summarization fails, falls back to simple truncation.

---

## 1. Core Interface

```cpp
// src/agent/ContextCompressor.h
#pragma once
#include "core/IProvider.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <vector>
#include <string>

namespace ea::agent {

struct CompressionConfig {
    int max_tokens = 8000;          // Token threshold to trigger compression
    int keep_recent_turns = 4;      // Keep last N turns (1 turn = 1 user + 1 assistant)
    bool enable = true;             // Enable/disable compression
};

class ContextCompressor {
public:
    explicit ContextCompressor(IProvider* provider, CompressionConfig config);

    // Compress message list if over threshold, returns compressed copy
    Result<std::vector<Message>> compress(const std::vector<Message>& messages);

    // Estimate token count (heuristic: 4 chars ≈ 1 token for English)
    static int estimate_tokens(const std::vector<Message>& messages);

    // Estimate tokens for a single message
    static int estimate_tokens(const Message& msg);

private:
    // Summarize a range of messages using LLM
    Result<std::string> summarize(const std::vector<Message>& messages);

    // Split messages into head (system), middle (to compress), tail (recent)
    struct SplitResult {
        std::vector<Message> head;
        std::vector<Message> middle;
        std::vector<Message> tail;
    };
    SplitResult split_messages(const std::vector<Message>& messages) const;

    IProvider* provider_;
    CompressionConfig config_;
};

}  // namespace ea::agent
```

---

## 2. Token Estimation

Simple heuristic — no external tokenizer dependency:

```cpp
int ContextCompressor::estimate_tokens(const Message& msg) {
    // Rough: 4 chars ≈ 1 token (English), 2 chars ≈ 1 token (CJK)
    int count = 0;
    for (char c : msg.content) {
        if (static_cast<unsigned char>(c) >= 0xC0) {
            count += 2;  // CJK/multibyte: ~0.5 tokens per char
        } else {
            count++;     // ASCII: ~0.25 tokens per char
        }
    }
    // Divide: ASCII chars count as 1, CJK as 2, then /4
    return (count + 3) / 4;  // Round up
}

int ContextCompressor::estimate_tokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        total += estimate_tokens(msg);
    }
    return total;
}
```

---

## 3. Compression Flow

```
CallProviderStep::execute():
  1. Build full message list (system + history)
  2. If compressor enabled:
     a. estimate_tokens(messages)
     b. if tokens > max_tokens:
        i.   split_messages() → head + middle + tail
        ii.  summarize(middle) → summary string
        iii. If summarize succeeded:
              messages = head + [System: summary] + tail
            Else (summarize failed):
              Fall back to simple truncation (keep head + tail only)
  3. Call provider->chat(messages, ...)
```

### split_messages Logic

```
1. head = messages[0] if role == System, else empty
2. tail_start = messages.size() - (keep_recent_turns * 2)  // 2 msgs per turn
3. tail_start = max(tail_start, 1)  // At least keep system message
4. middle = messages[1..tail_start)
5. tail = messages[tail_start..end]
```

If middle is empty (conversation too short), no compression needed.

### Summarization Prompt

```cpp
Result<std::string> ContextCompressor::summarize(const std::vector<Message>& messages) {
    std::string prompt = "Summarize the following conversation concisely, "
        "preserving key facts, decisions, and outcomes. "
        "Omit greetings and repetitions.\n\n";

    for (const auto& msg : messages) {
        prompt += role_name(msg.role) + ": " + msg.content + "\n\n";
    }

    // Call LLM with summarization request
    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };

    auto response = provider_->chat(req, {}, "summarize", {});
    if (!response.ok()) return response.error();

    return response.value().content;
}
```

---

## 4. CallProviderStep Integration

Modify `CallProviderStep` to accept an optional `ContextCompressor*`:

```cpp
// src/agent/steps/CallProviderStep.h (modified)
#pragma once
#include "agent/ITurnStep.h"
#include "agent/ContextCompressor.h"

namespace ea::agent {

class CallProviderStep : public ITurnStep {
public:
    explicit CallProviderStep(ContextCompressor* compressor = nullptr);

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "call_provider"; }

private:
    ContextCompressor* compressor_;
};

}
```

**Execution flow:**

```cpp
Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::invalid_arg("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, ...});
    }
    for (const auto& msg : ctx.messages) {
        messages.push_back(msg);
    }

    // Compress if needed
    if (compressor_) {
        auto compressed = compressor_->compress(messages);
        if (compressed.ok()) {
            messages = std::move(compressed.value());
        }
        // If compression fails, use original messages (graceful degradation)
    }

    // Call provider
    ChatOptions opts;
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) {
        EA_ERROR("LLM call failed: {}", response.error().message);
        return response.error();
    }

    ctx.response = std::move(response.value());
    return {};
}
```

---

## 5. AgentLoop Integration

Add `ContextCompressor*` to `AgentLoop` constructor:

```cpp
AgentLoop(IProvider* provider,
          ToolRegistry* registry,
          IMemory* memory,
          Config config,
          OutputFn output,
          security::SecurityPolicy* policy = nullptr,
          security::IApprovalHandler* approval = nullptr,
          ContextCompressor* compressor = nullptr);
```

Pass `compressor_` to `CallProviderStep` in the step chain:

```cpp
steps_.push_back(std::make_unique<CallProviderStep>(compressor_));
```

---

## 6. Configuration

### TOML Config Extension

```toml
[agent]
max_iterations = 90
auto_memory = true

[agent.compression]
enable = true
max_tokens = 8000
keep_recent_turns = 4
```

### AgentConfig Extension

```cpp
struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    // Compression
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
};
```

### main.cpp Integration

```cpp
// Create context compressor
std::unique_ptr<ea::agent::ContextCompressor> compressor;
if (cfg.agent.compression_enable) {
    ea::agent::CompressionConfig comp_cfg;
    comp_cfg.max_tokens = cfg.agent.compression_max_tokens;
    comp_cfg.keep_recent_turns = cfg.agent.compression_keep_recent_turns;
    compressor = std::make_unique<ea::agent::ContextCompressor>(provider.get(), comp_cfg);
}

// Pass to AgentLoop
ea::agent::AgentLoop loop(
    provider.get(), &registry, memory.get(),
    ea::agent::AgentLoop::Config{cfg.agent.max_iterations},
    [](const std::string& text) { std::cout << text << std::endl; },
    security.get(),
    approval.get(),
    compressor.get()
);
```

---

## 7. Memory Integration

Before compression, call `memory->on_pre_compress()` to allow the memory system to store a summary of about-to-be-compressed conversation. This is already defined in `IMemory` as a default no-op hook — `SqliteMemory` can override it to persist key information.

---

## 8. Test Strategy

### Unit Tests

| Test File | Coverage |
|-----------|----------|
| `tests/test_context_compressor.cpp` | Token estimation, split logic, compression with mock provider |

### Key Test Cases

1. `estimate_tokens` — ASCII text: ~4 chars per token
2. `estimate_tokens` — CJK text: ~2 chars per token
3. `estimate_tokens` — empty messages = 0 tokens
4. `split_messages` — preserves system message in head
5. `split_messages` — keeps recent N turns in tail
6. `split_messages` — middle contains remaining messages
7. `compress` — under threshold: returns messages unchanged
8. `compress` — over threshold: returns compressed messages with summary
9. `compress` — summarization failure: falls back to truncation
10. `compress` — disabled: returns messages unchanged
11. `compress` — very short conversation: no compression needed

### Integration Test

Add to `test_integration_provider_tool.cpp`:
- Long conversation that triggers compression, verify messages are shorter after compression

---

## 9. File Manifest

### New Files

| File | Responsibility |
|------|---------------|
| `src/agent/ContextCompressor.h` | Compression interface + config |
| `src/agent/ContextCompressor.cpp` | Implementation (estimation, split, summarize, compress) |
| `tests/test_context_compressor.cpp` | Unit tests |

### Modified Files

| File | Change |
|------|--------|
| `src/agent/steps/CallProviderStep.h` | Add ContextCompressor* constructor param |
| `src/agent/steps/CallProviderStep.cpp` | Compress messages before calling provider |
| `src/agent/AgentLoop.h` | Add ContextCompressor* constructor param |
| `src/agent/AgentLoop.cpp` | Pass compressor to CallProviderStep |
| `src/agent/CMakeLists.txt` | Add ContextCompressor.cpp |
| `src/config/Config.h` | AgentConfig adds compression fields |
| `src/config/Config.cpp` | Parse [agent.compression] TOML section |
| `src/main.cpp` | Create ContextCompressor, pass to AgentLoop |
| `tests/CMakeLists.txt` | Add test_context_compressor.cpp |

---

## Scope Summary

| Dimension | Content |
|-----------|---------|
| Goal | LLM-based context compression for long conversations |
| Trigger | Token count > max_tokens before LLM call |
| Strategy | Head (system) + Summary (middle) + Tail (recent N turns) |
| Integration point | CallProviderStep, before provider->chat() |
| Fallback | Simple truncation if summarization fails |
| Memory hook | on_pre_compress() before compression |
| Config | compression_enable / compression_max_tokens / compression_keep_recent_turns |

**Not in scope:** External tokenizer (tiktoken), streaming compression, compression caching, multi-model summarization.
