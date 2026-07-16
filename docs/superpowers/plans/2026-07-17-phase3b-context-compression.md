# Phase 3B: LLM Context Compression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add LLM-based context compression that summarizes middle conversation turns when token count exceeds a threshold, preserving system prompt and recent turns.

**Architecture:** ContextCompressor estimates tokens, splits messages into head/middle/tail, summarizes middle via LLM, and rebuilds the message list. Integrated into CallProviderStep before each LLM call. Operates on a copy — original history is never modified.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, Catch2

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/agent/ContextCompressor.h` | CompressionConfig + ContextCompressor class declaration |
| `src/agent/ContextCompressor.cpp` | Implementation: estimate_tokens, split_messages, summarize, compress |
| `tests/test_context_compressor.cpp` | Unit tests for token estimation, split, compression |

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

### Task 1: ContextCompressor Header

**Files:**
- Create: `src/agent/ContextCompressor.h`

- [ ] **Step 1: Write ContextCompressor.h**

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

    // Estimate token count (heuristic: 4 chars ≈ 1 token for ASCII, 2 chars ≈ 1 token for CJK)
    static int estimate_tokens(const std::vector<Message>& messages);

    // Estimate tokens for a single message
    static int estimate_tokens(const Message& msg);

    const CompressionConfig& config() const { return config_; }

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

- [ ] **Step 2: Commit**

```bash
git add src/agent/ContextCompressor.h
git commit -m "feat(agent): add ContextCompressor header with CompressionConfig"
```

---

### Task 2: ContextCompressor Implementation

**Files:**
- Create: `src/agent/ContextCompressor.cpp`
- Modify: `src/agent/CMakeLists.txt`

- [ ] **Step 1: Write ContextCompressor.cpp**

```cpp
// src/agent/ContextCompressor.cpp
#include "ContextCompressor.h"
#include "common/io/Logger.h"

namespace ea::agent {

ContextCompressor::ContextCompressor(IProvider* provider, CompressionConfig config)
    : provider_(provider), config_(std::move(config)) {}

int ContextCompressor::estimate_tokens(const Message& msg) {
    // Heuristic: 4 chars ≈ 1 token (ASCII), 2 chars ≈ 1 token (CJK/multibyte)
    int weighted_chars = 0;
    for (unsigned char c : msg.content) {
        if (c >= 0xC0) {
            weighted_chars += 2;  // CJK/multibyte start byte
        } else {
            weighted_chars += 1;  // ASCII
        }
    }
    return (weighted_chars + 3) / 4;  // Round up
}

int ContextCompressor::estimate_tokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        total += estimate_tokens(msg);
    }
    return total;
}

ContextCompressor::SplitResult
ContextCompressor::split_messages(const std::vector<Message>& messages) const {
    SplitResult result;

    if (messages.empty()) return result;

    // Head: system message(s) at the beginning
    size_t i = 0;
    while (i < messages.size() && messages[i].role == Role::System) {
        result.head.push_back(messages[i]);
        ++i;
    }

    // Tail: last keep_recent_turns * 2 messages (user + assistant per turn)
    size_t tail_count = static_cast<size_t>(config_.keep_recent_turns) * 2;
    size_t tail_start = messages.size();
    if (tail_start > tail_count + i) {
        tail_start = messages.size() - tail_count;
    } else {
        tail_start = i;  // Not enough messages for tail, keep all from i
    }

    // Middle: everything between head and tail
    for (size_t j = i; j < tail_start; ++j) {
        result.middle.push_back(messages[j]);
    }

    // Tail
    for (size_t j = tail_start; j < messages.size(); ++j) {
        result.tail.push_back(messages[j]);
    }

    return result;
}

Result<std::string> ContextCompressor::summarize(const std::vector<Message>& messages) {
    if (!provider_) {
        return Error::invalid_arg("No provider for summarization");
    }

    std::string prompt = "Summarize the following conversation concisely, "
        "preserving key facts, decisions, and outcomes. "
        "Omit greetings and repetitions.\n\n";

    for (const auto& msg : messages) {
        const char* role_name = "unknown";
        switch (msg.role) {
            case Role::System:    role_name = "System"; break;
            case Role::User:      role_name = "User"; break;
            case Role::Assistant: role_name = "Assistant"; break;
            case Role::Tool:      role_name = "Tool"; break;
        }
        prompt += role_name;
        prompt += ": ";
        prompt += msg.content;
        prompt += "\n\n";
    }

    // Call LLM with summarization request
    std::vector<Message> req = {
        {Role::System, "You are a conversation summarizer. Be concise and factual.",
         std::nullopt, std::nullopt, std::nullopt},
        {Role::User, prompt, std::nullopt, std::nullopt, std::nullopt}
    };

    ChatOptions opts;
    auto response = provider_->chat(req, {}, "", opts);
    if (!response.ok()) {
        EA_WARN("Summarization failed: {}", response.error().message);
        return response.error();
    }

    EA_DEBUG("Summarized {} messages into {} tokens",
             messages.size(), estimate_tokens(response.value().content));
    return response.value().content;
}

Result<std::vector<Message>> ContextCompressor::compress(const std::vector<Message>& messages) {
    if (!config_.enable) {
        return messages;  // Compression disabled
    }

    int tokens = estimate_tokens(messages);
    if (tokens <= config_.max_tokens) {
        return messages;  // Under threshold, no compression needed
    }

    EA_INFO("Context compression triggered: {} tokens > {} max_tokens",
            tokens, config_.max_tokens);

    auto split = split_messages(messages);

    if (split.middle.empty()) {
        // No middle to compress
        return messages;
    }

    // Try LLM summarization
    auto summary_result = summarize(split.middle);
    if (summary_result.ok()) {
        // Build compressed message list: head + summary + tail
        std::vector<Message> compressed;
        compressed.reserve(split.head.size() + 1 + split.tail.size());

        for (const auto& msg : split.head) {
            compressed.push_back(msg);
        }

        // Insert summary as a system message
        compressed.push_back({
            Role::System,
            "[Conversation Summary]\n" + summary_result.value(),
            std::nullopt, std::nullopt, std::nullopt
        });

        for (const auto& msg : split.tail) {
            compressed.push_back(msg);
        }

        int new_tokens = estimate_tokens(compressed);
        EA_INFO("Context compressed: {} tokens → {} tokens ({}% reduction)",
                tokens, new_tokens,
                (100 * (tokens - new_tokens) / tokens));

        return compressed;
    }

    // Fallback: simple truncation (keep head + tail, drop middle)
    EA_WARN("Summarization failed, falling back to simple truncation");
    std::vector<Message> truncated;
    truncated.reserve(split.head.size() + 1 + split.tail.size());

    for (const auto& msg : split.head) {
        truncated.push_back(msg);
    }

    truncated.push_back({
        Role::System,
        "[Earlier conversation history pruned to fit context window]",
        std::nullopt, std::nullopt, std::nullopt
    });

    for (const auto& msg : split.tail) {
        truncated.push_back(msg);
    }

    return truncated;
}

}  // namespace ea::agent
```

- [ ] **Step 2: Update src/agent/CMakeLists.txt**

Add `ContextCompressor.cpp` to the OBJECT library:

```cmake
add_library(ea-agent OBJECT
    AgentLoop.cpp
    SystemPrompt.cpp
    LoopDetector.cpp
    ContextCompressor.cpp
    steps/CallProviderStep.cpp
    steps/LoopDetectStep.cpp
    steps/ExecuteToolsStep.cpp
)
```

- [ ] **Step 3: Build to verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc)`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/agent/ContextCompressor.cpp src/agent/CMakeLists.txt
git commit -m "feat(agent): implement ContextCompressor with token estimation and LLM summarization"
```

---

### Task 3: ContextCompressor Tests

**Files:**
- Create: `tests/test_context_compressor.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write test file**

```cpp
// tests/test_context_compressor.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/ContextCompressor.h"
#include "core/IProvider.h"

using namespace ea;
using namespace ea::agent;

// --- Token estimation tests ---

TEST_CASE("estimate_tokens for ASCII text", "[compression]") {
    Message msg{Role::User, "Hello world test", std::nullopt, std::nullopt, std::nullopt};
    int tokens = ContextCompressor::estimate_tokens(msg);
    // 16 chars / 4 ≈ 4 tokens
    REQUIRE(tokens >= 3);
    REQUIRE(tokens <= 6);
}

TEST_CASE("estimate_tokens for empty message", "[compression]") {
    Message msg{Role::User, "", std::nullopt, std::nullopt, std::nullopt};
    REQUIRE(ContextCompressor::estimate_tokens(msg) == 0);
}

TEST_CASE("estimate_tokens for empty message list", "[compression]") {
    REQUIRE(ContextCompressor::estimate_tokens(std::vector<Message>{}) == 0);
}

TEST_CASE("estimate_tokens for multiple messages", "[compression]") {
    std::vector<Message> msgs = {
        {Role::System, "You are helpful.", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "Hi there!", std::nullopt, std::nullopt, std::nullopt}
    };
    int total = ContextCompressor::estimate_tokens(msgs);
    int sum = ContextCompressor::estimate_tokens(msgs[0])
            + ContextCompressor::estimate_tokens(msgs[1])
            + ContextCompressor::estimate_tokens(msgs[2]);
    REQUIRE(total == sum);
}

// --- Compression tests with mock provider ---

class MockSummaryProvider : public IProvider {
public:
    std::string name() const override { return "mock-summary"; }
    std::vector<std::string> list_models() const override { return {}; }
    provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    Result<LLMResponse> chat(const std::vector<Message>& msgs,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        call_count_++;
        last_messages_ = msgs;
        if (fail_) return Error::net("summarization failed");

        LLMResponse resp;
        resp.content = "Summary of conversation: user asked about testing.";
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return Error::net("not implemented");
    }

    int call_count() const { return call_count_; }
    const std::vector<Message>& last_messages() const { return last_messages_; }
    void set_fail(bool f) { fail_ = f; }

private:
    int call_count_ = 0;
    std::vector<Message> last_messages_;
    bool fail_ = false;
};

TEST_CASE("compress returns messages unchanged when under threshold", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10000;  // High threshold
    config.keep_recent_turns = 4;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs = {
        {Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt},
        {Role::Assistant, "Hi!", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 3);
    REQUIRE(provider->call_count() == 0);  // No LLM call needed
}

TEST_CASE("compress returns messages unchanged when disabled", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.enable = false;
    config.max_tokens = 1;  // Low threshold but disabled

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs = {
        {Role::System, "System", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2);
    REQUIRE(provider->call_count() == 0);
}

TEST_CASE("compress triggers summarization when over threshold", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10;  // Very low threshold to force compression
    config.keep_recent_turns = 1;  // Keep only 1 turn (2 messages)

    ContextCompressor compressor(provider.get(), config);

    // Build a long conversation
    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 10; ++i) {
        msgs.push_back({Role::User, "Question " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "Answer " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(provider->call_count() == 1);  // LLM was called for summarization
    // Result should be smaller than original
    REQUIRE(result.value().size() < msgs.size());
    // First message should still be system prompt
    REQUIRE(result.value()[0].role == Role::System);
    // Should contain summary marker
    bool found_summary = false;
    for (const auto& m : result.value()) {
        if (m.content.find("[Conversation Summary]") != std::string::npos) {
            found_summary = true;
        }
    }
    REQUIRE(found_summary);
}

TEST_CASE("compress falls back to truncation on summarization failure", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    provider->set_fail(true);

    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 1;

    ContextCompressor compressor(provider.get(), config);

    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System prompt", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 10; ++i) {
        msgs.push_back({Role::User, "Question " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "Answer " + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() < msgs.size());
    // Should contain truncation breadcrumb
    bool found_breadcrumb = false;
    for (const auto& m : result.value()) {
        if (m.content.find("pruned") != std::string::npos) {
            found_breadcrumb = true;
        }
    }
    REQUIRE(found_breadcrumb);
}

TEST_CASE("compress with null provider falls back to truncation", "[compression]") {
    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 1;

    ContextCompressor compressor(nullptr, config);

    std::vector<Message> msgs;
    msgs.push_back({Role::System, "System", std::nullopt, std::nullopt, std::nullopt});
    for (int i = 0; i < 5; ++i) {
        msgs.push_back({Role::User, "Q" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
        msgs.push_back({Role::Assistant, "A" + std::to_string(i), std::nullopt, std::nullopt, std::nullopt});
    }

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() < msgs.size());
}

TEST_CASE("compress does nothing for short conversation", "[compression]") {
    auto provider = std::make_shared<MockSummaryProvider>();
    CompressionConfig config;
    config.max_tokens = 10;
    config.keep_recent_turns = 4;

    ContextCompressor compressor(provider.get(), config);

    // Only 2 messages, all in tail
    std::vector<Message> msgs = {
        {Role::System, "System", std::nullopt, std::nullopt, std::nullopt},
        {Role::User, "Hello", std::nullopt, std::nullopt, std::nullopt}
    };

    auto result = compressor.compress(msgs);
    REQUIRE(result.ok());
    // Middle is empty, so no compression even though over threshold
    REQUIRE(result.value().size() == 2);
}

TEST_CASE("CompressionConfig default values", "[compression]") {
    CompressionConfig config;
    REQUIRE(config.max_tokens == 8000);
    REQUIRE(config.keep_recent_turns == 4);
    REQUIRE(config.enable == true);
}
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

Add `test_context_compressor.cpp` to the `add_executable(ea-tests ...)` source list.

- [ ] **Step 3: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[compression]" --reporter compact`
Expected: All 11 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_context_compressor.cpp tests/CMakeLists.txt
git commit -m "test(agent): add ContextCompressor unit tests"
```

---

### Task 4: CallProviderStep Integration

**Files:**
- Modify: `src/agent/steps/CallProviderStep.h`
- Modify: `src/agent/steps/CallProviderStep.cpp`

- [ ] **Step 1: Update CallProviderStep.h**

```cpp
// CallProviderStep — invoke LLM provider with optional context compression
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

}  // namespace ea::agent
```

- [ ] **Step 2: Update CallProviderStep.cpp**

```cpp
#include "CallProviderStep.h"
#include "common/io/Logger.h"

namespace ea::agent {

CallProviderStep::CallProviderStep(ContextCompressor* compressor)
    : compressor_(compressor) {}

Result<void> CallProviderStep::execute(TurnContext& ctx) {
    if (!ctx.provider) {
        return Error::invalid_arg("No provider configured");
    }

    // Build full message list with system prompt
    std::vector<Message> messages;
    if (!ctx.system_prompt.empty()) {
        messages.push_back({Role::System, ctx.system_prompt, std::nullopt, std::nullopt, std::nullopt});
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

    ChatOptions opts;
    auto response = ctx.provider->chat(messages, ctx.tool_specs, "", opts);
    if (!response.ok()) {
        EA_ERROR("LLM call failed: {}", response.error().message);
        return response.error();
    }

    ctx.response = std::move(response.value());
    return {};
}

}  // namespace ea::agent
```

- [ ] **Step 3: Build and run all tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/agent/steps/CallProviderStep.h src/agent/steps/CallProviderStep.cpp
git commit -m "feat(agent): integrate ContextCompressor into CallProviderStep"
```

---

### Task 5: AgentLoop Integration

**Files:**
- Modify: `src/agent/AgentLoop.h`
- Modify: `src/agent/AgentLoop.cpp`

- [ ] **Step 1: Update AgentLoop.h**

Add `#include "agent/ContextCompressor.h"` after the existing includes.

Update constructor to add `ContextCompressor* compressor = nullptr` as the last parameter:

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

Add private member: `ContextCompressor* compressor_;`

- [ ] **Step 2: Update AgentLoop.cpp constructor**

Update constructor signature and initializer list:

```cpp
AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output,
                     security::SecurityPolicy* policy,
                     security::IApprovalHandler* approval,
                     ContextCompressor* compressor)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
    , policy_(policy)
    , approval_(approval)
    , compressor_(compressor)
{
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>(compressor_));
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>(policy_, approval_));
    steps_.push_back(std::make_unique<LoopDetectStep>(loop_detector_));
    steps_.push_back(std::make_unique<CollectResultsStep>());
}
```

- [ ] **Step 3: Build and run all tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/agent/AgentLoop.h src/agent/AgentLoop.cpp
git commit -m "feat(agent): add ContextCompressor param to AgentLoop"
```

---

### Task 6: Config Extension

**Files:**
- Modify: `src/config/Config.h`
- Modify: `src/config/Config.cpp`

- [ ] **Step 1: Update AgentConfig in Config.h**

Add compression fields:

```cpp
struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    // Context compression
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
};
```

- [ ] **Step 2: Update Config.cpp**

After the `agent.soul` parsing line, add:

```cpp
            if (agent.contains("compression")) {
                auto compression = toml::find(agent, "compression");
                cfg.agent.compression_enable = toml::find_or<bool>(compression, "enable", cfg.agent.compression_enable);
                cfg.agent.compression_max_tokens = toml::find_or<int>(compression, "max_tokens", cfg.agent.compression_max_tokens);
                cfg.agent.compression_keep_recent_turns = toml::find_or<int>(compression, "keep_recent_turns", cfg.agent.compression_keep_recent_turns);
            }
```

- [ ] **Step 3: Build and run config tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[config]" --reporter compact`
Expected: All config tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp
git commit -m "feat(config): add compression config to AgentConfig"
```

---

### Task 7: main.cpp Integration

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update main.cpp**

Add include after existing ones:
```cpp
#include "agent/ContextCompressor.h"
```

After the approval handler creation (step 5.5), add step 5.6:
```cpp
    // 5.6. Create context compressor
    std::unique_ptr<ea::agent::ContextCompressor> compressor;
    if (cfg.agent.compression_enable) {
        ea::agent::CompressionConfig comp_cfg;
        comp_cfg.max_tokens = cfg.agent.compression_max_tokens;
        comp_cfg.keep_recent_turns = cfg.agent.compression_keep_recent_turns;
        compressor = std::make_unique<ea::agent::ContextCompressor>(provider.get(), comp_cfg);
    }
```

Update AgentLoop creation to pass compressor:
```cpp
    ea::agent::AgentLoop loop(
        provider.get(), &registry, memory.get(),
        ea::agent::AgentLoop::Config{cfg.agent.max_iterations},
        [](const std::string& text) {
            std::cout << text << std::endl;
        },
        security.get(),
        approval.get(),
        compressor.get()
    );
```

- [ ] **Step 2: Build and run all tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate ContextCompressor into main.cpp"
```

---

### Task 8: Final Verification

- [ ] **Step 1: Clean rebuild + full test suite**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 2: Verify no compiler warnings in ea-* code**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep "warning:" | grep -v "mbedtls" | grep -v "third_party"`
Expected: Zero warnings in project code.
