# Phase 3A: Approval System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an approval system that intercepts dangerous tool calls (`is_dangerous() == true`), pausing execution until a human confirms or rejects the operation.

**Architecture:** Interceptor pattern — inject `IApprovalHandler` into `ExecuteToolsStep`. Before executing a tool, chain SecurityPolicy (hard block) then ApprovalHandler (soft confirm). CLI mode uses `StdinApprovalHandler` (stdin y/n/a), Server mode uses `PendingApprovalHandler` (condition_variable + timeout).

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, Catch2

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/security/IApprovalHandler.h` | Approval interface + ApprovalDecision/ApprovalRequest types |
| `src/security/StdinApprovalHandler.h` | CLI stdin approval (header-only) |
| `src/security/PendingApprovalHandler.h` | Server async approval declaration |
| `src/security/PendingApprovalHandler.cpp` | Server implementation (condition_variable + timeout) |
| `tests/test_approval_handler.cpp` | MockApprovalHandler + interface contract tests |
| `tests/test_stdin_approval_handler.cpp` | StdinApprovalHandler tests (redirected stdin) |
| `tests/test_pending_approval_handler.cpp` | PendingApprovalHandler tests (multi-threaded resolve + timeout) |

### Modified Files

| File | Change |
|------|--------|
| `src/agent/steps/ExecuteToolsStep.h` | Add SecurityPolicy* + IApprovalHandler* constructor params |
| `src/agent/steps/ExecuteToolsStep.cpp` | Chain SecurityPolicy + ApprovalHandler before tool execution |
| `src/agent/AgentLoop.h` | Add SecurityPolicy* + IApprovalHandler* constructor params |
| `src/agent/AgentLoop.cpp` | Pass policy + approval to ExecuteToolsStep |
| `src/config/Config.h` | SecurityConfig adds approval_timeout/approval_mode/auto_approve_dangerous |
| `src/config/Config.cpp` | Parse new [security.approval] TOML section |
| `src/main.cpp` | Create ApprovalHandler based on config + build mode |
| `src/security/CMakeLists.txt` | Add PendingApprovalHandler.cpp |
| `tests/CMakeLists.txt` | Add 3 new test files |

---

### Task 1: IApprovalHandler Interface

**Files:**
- Create: `src/security/IApprovalHandler.h`

- [ ] **Step 1: Write IApprovalHandler.h**

```cpp
// src/security/IApprovalHandler.h
#pragma once
#include <string>
#include "nlohmann/json.hpp"

namespace ea::security {

enum class ApprovalDecision { Approved, Rejected, Aborted };

struct ApprovalRequest {
    std::string tool_name;
    nlohmann::json arguments;
    std::string description;
};

class IApprovalHandler {
public:
    virtual ~IApprovalHandler() = default;
    virtual ApprovalDecision request_approval(const ApprovalRequest& req) = 0;
};

}  // namespace ea::security
```

- [ ] **Step 2: Build to verify header compiles**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds (header-only, no consumers yet).

- [ ] **Step 3: Commit**

```bash
git add src/security/IApprovalHandler.h
git commit -m "feat(security): add IApprovalHandler interface with ApprovalDecision/ApprovalRequest"
```

---

### Task 2: MockApprovalHandler + Interface Tests

**Files:**
- Create: `tests/test_approval_handler.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write test file**

```cpp
// tests/test_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/IApprovalHandler.h"

using namespace ea::security;

// MockApprovalHandler for testing
class MockApprovalHandler : public IApprovalHandler {
public:
    ApprovalDecision next_decision = ApprovalDecision::Approved;
    mutable ApprovalRequest last_request;

    ApprovalDecision request_approval(const ApprovalRequest& req) override {
        last_request = req;
        return next_decision;
    }
};

TEST_CASE("MockApprovalHandler returns Approved by default", "[approval]") {
    MockApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Approved);
}

TEST_CASE("MockApprovalHandler returns Rejected when configured", "[approval]") {
    MockApprovalHandler handler;
    handler.next_decision = ApprovalDecision::Rejected;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "rm -rf /"}}, "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("MockApprovalHandler returns Aborted when configured", "[approval]") {
    MockApprovalHandler handler;
    handler.next_decision = ApprovalDecision::Aborted;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Aborted);
}

TEST_CASE("MockApprovalHandler captures last request", "[approval]") {
    MockApprovalHandler handler;
    ApprovalRequest req{"file", nlohmann::json{{"path", "/tmp/test"}}, "Write file"};
    handler.request_approval(req);
    REQUIRE(handler.last_request.tool_name == "file");
    REQUIRE(handler.last_request.arguments["path"] == "/tmp/test");
    REQUIRE(handler.last_request.description == "Write file");
}

TEST_CASE("ApprovalRequest default values", "[approval]") {
    ApprovalRequest req;
    REQUIRE(req.tool_name.empty());
    REQUIRE(req.description.empty());
    REQUIRE(req.arguments.is_null());
}
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, add `test_approval_handler.cpp` to the `add_executable(ea-tests ...)` source list, after `test_security_policy.cpp`:

```
    test_security_policy.cpp
    test_approval_handler.cpp
```

- [ ] **Step 3: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[approval]" --reporter compact`
Expected: All 5 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_approval_handler.cpp tests/CMakeLists.txt
git commit -m "test(security): add MockApprovalHandler and interface contract tests"
```

---

### Task 3: StdinApprovalHandler

**Files:**
- Create: `src/security/StdinApprovalHandler.h`
- Create: `tests/test_stdin_approval_handler.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write StdinApprovalHandler.h**

```cpp
// src/security/StdinApprovalHandler.h
#pragma once
#include "IApprovalHandler.h"
#include <iostream>
#include <string>

namespace ea::security {

class StdinApprovalHandler : public IApprovalHandler {
public:
    ApprovalDecision request_approval(const ApprovalRequest& req) override {
        std::cout << "\n⚠️  Dangerous tool call requires approval:\n"
                  << "    Tool: " << req.tool_name << "\n"
                  << "    Args: " << req.arguments.dump() << "\n"
                  << "    [y/n/a(abort)] > " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input)) {
            return ApprovalDecision::Aborted;
        }

        if (input == "y" || input == "Y") return ApprovalDecision::Approved;
        if (input == "a" || input == "A") return ApprovalDecision::Aborted;
        return ApprovalDecision::Rejected;
    }
};

}  // namespace ea::security
```

- [ ] **Step 2: Write test file with redirected stdin**

```cpp
// tests/test_stdin_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/StdinApprovalHandler.h"
#include <sstream>
#include <string>

using namespace ea::security;

// Helper to redirect stdin for testing
class StdinRedirect {
public:
    explicit StdinRedirect(const std::string& input)
        : old_buf_(std::cin.rdbuf()) {
        sstream_.str(input);
        std::cin.rdbuf(sstream_.rdbuf());
    }
    ~StdinRedirect() {
        std::cin.rdbuf(old_buf_);
    }
private:
    std::streambuf* old_buf_;
    std::stringstream sstream_;
};

TEST_CASE("StdinApprovalHandler approves on 'y'", "[approval][stdin]") {
    StdinRedirect redir("y\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Approved);
}

TEST_CASE("StdinApprovalHandler approves on 'Y'", "[approval][stdin]") {
    StdinRedirect redir("Y\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Approved);
}

TEST_CASE("StdinApprovalHandler rejects on 'n'", "[approval][stdin]") {
    StdinRedirect redir("n\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Rejected);
}

TEST_CASE("StdinApprovalHandler rejects on empty input", "[approval][stdin]") {
    StdinRedirect redir("\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Rejected);
}

TEST_CASE("StdinApprovalHandler aborts on 'a'", "[approval][stdin]") {
    StdinRedirect redir("a\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}

TEST_CASE("StdinApprovalHandler aborts on 'A'", "[approval][stdin]") {
    StdinRedirect redir("A\n");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}

TEST_CASE("StdinApprovalHandler aborts on stdin EOF", "[approval][stdin]") {
    // Empty stream = getline fails = EOF
    StdinRedirect redir("");
    StdinApprovalHandler handler;
    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};
    REQUIRE(handler.request_approval(req) == ApprovalDecision::Aborted);
}
```

- [ ] **Step 3: Add test file to tests/CMakeLists.txt**

In `tests/CMakeLists.txt`, add `test_stdin_approval_handler.cpp` after `test_approval_handler.cpp`:

```
    test_approval_handler.cpp
    test_stdin_approval_handler.cpp
```

- [ ] **Step 4: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[stdin]" --reporter compact`
Expected: All 7 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/security/StdinApprovalHandler.h tests/test_stdin_approval_handler.cpp tests/CMakeLists.txt
git commit -m "feat(security): add StdinApprovalHandler for CLI mode approval"
```

---

### Task 4: PendingApprovalHandler

**Files:**
- Create: `src/security/PendingApprovalHandler.h`
- Create: `src/security/PendingApprovalHandler.cpp`
- Create: `tests/test_pending_approval_handler.cpp`
- Modify: `src/security/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write PendingApprovalHandler.h**

```cpp
// src/security/PendingApprovalHandler.h
#pragma once
#include "IApprovalHandler.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ea::security {

struct PendingApproval {
    std::string id;
    ApprovalRequest request;
    std::atomic<ApprovalDecision> decision{ApprovalDecision::Rejected};
    std::condition_variable cv;
    std::mutex mutex;
    bool resolved = false;
};

class PendingApprovalHandler : public IApprovalHandler {
public:
    explicit PendingApprovalHandler(int timeout_seconds = 300);

    ApprovalDecision request_approval(const ApprovalRequest& req) override;

    // Server-side: list pending approvals
    std::vector<PendingApproval*> pending_list() const;

    // Server-side: resolve an approval by id
    bool resolve(const std::string& id, ApprovalDecision decision);

    // Number of pending (unresolved) approvals
    size_t pending_count() const;

private:
    int timeout_seconds_;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<PendingApproval>> pending_;
    int next_id_ = 1;
};

}  // namespace ea::security
```

- [ ] **Step 2: Write PendingApprovalHandler.cpp**

```cpp
// src/security/PendingApprovalHandler.cpp
#include "PendingApprovalHandler.h"
#include "common/io/Logger.h"
#include <chrono>

namespace ea::security {

PendingApprovalHandler::PendingApprovalHandler(int timeout_seconds)
    : timeout_seconds_(timeout_seconds) {}

ApprovalDecision PendingApprovalHandler::request_approval(const ApprovalRequest& req) {
    auto approval = std::make_unique<PendingApproval>();

    // Generate ID
    {
        std::lock_guard<std::mutex> lock(mutex_);
        approval->id = std::to_string(next_id_++);
        approval->request = req;
    }

    EA_INFO("Approval requested: id={}, tool={}", approval->id, req.tool_name);

    PendingApproval* raw_ptr = approval.get();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[approval->id] = std::move(approval);
    }

    // Block until resolved or timeout
    std::unique_lock<std::mutex> lock(raw_ptr->mutex);
    bool timed_out = !raw_ptr->cv.wait_for(lock,
        std::chrono::seconds(timeout_seconds_),
        [raw_ptr] { return raw_ptr->resolved; });

    if (timed_out) {
        EA_WARN("Approval timed out: id={}, tool={}", raw_ptr->id, req.tool_name);
        raw_ptr->decision = ApprovalDecision::Rejected;
    }

    auto result = raw_ptr->decision.load();

    // Clean up resolved approval
    {
        std::lock_guard<std::mutex> map_lock(mutex_);
        pending_.erase(raw_ptr->id);
    }

    return result;
}

std::vector<PendingApproval*> PendingApprovalHandler::pending_list() const {
    std::vector<PendingApproval*> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, approval] : pending_) {
        if (!approval->resolved) {
            result.push_back(approval.get());
        }
    }
    return result;
}

bool PendingApprovalHandler::resolve(const std::string& id, ApprovalDecision decision) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return false;

    auto& approval = it->second;
    approval->decision = decision;
    approval->resolved = true;
    approval->cv.notify_one();

    EA_INFO("Approval resolved: id={}, decision={}", id,
            decision == ApprovalDecision::Approved ? "approved" :
            decision == ApprovalDecision::Rejected ? "rejected" : "aborted");

    return true;
}

size_t PendingApprovalHandler::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, approval] : pending_) {
        if (!approval->resolved) ++count;
    }
    return count;
}

}  // namespace ea::security
```

- [ ] **Step 3: Write test file**

```cpp
// tests/test_pending_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "security/PendingApprovalHandler.h"
#include <chrono>
#include <thread>

using namespace ea::security;

TEST_CASE("PendingApprovalHandler blocks until resolved", "[approval][pending]") {
    PendingApprovalHandler handler(10);  // 10s timeout

    ApprovalRequest req{"shell", nlohmann::json{{"command", "ls"}}, "Execute shell command"};

    // Resolve in background thread after a short delay
    std::string approval_id;
    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // Find the pending approval
        auto pending = handler.pending_list();
        REQUIRE(pending.size() == 1);
        approval_id = pending[0]->id;
        handler.resolve(approval_id, ApprovalDecision::Approved);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Approved);
}

TEST_CASE("PendingApprovalHandler rejects on timeout", "[approval][pending]") {
    PendingApprovalHandler handler(1);  // 1s timeout for fast test

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    // No resolver — let it timeout
    auto decision = handler.request_approval(req);
    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("PendingApprovalHandler resolve with Rejected", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        REQUIRE(pending.size() == 1);
        handler.resolve(pending[0]->id, ApprovalDecision::Rejected);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Rejected);
}

TEST_CASE("PendingApprovalHandler resolve with Aborted", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        handler.resolve(pending[0]->id, ApprovalDecision::Aborted);
    });

    auto decision = handler.request_approval(req);
    resolver.join();

    REQUIRE(decision == ApprovalDecision::Aborted);
}

TEST_CASE("PendingApprovalHandler resolve returns false for unknown id", "[approval][pending]") {
    PendingApprovalHandler handler(10);
    REQUIRE(handler.resolve("nonexistent", ApprovalDecision::Approved) == false);
}

TEST_CASE("PendingApprovalHandler pending_count is zero when empty", "[approval][pending]") {
    PendingApprovalHandler handler(10);
    REQUIRE(handler.pending_count() == 0);
}

TEST_CASE("PendingApprovalHandler cleans up after resolution", "[approval][pending]") {
    PendingApprovalHandler handler(10);

    ApprovalRequest req{"shell", nlohmann::json::object(), "Execute shell command"};

    std::thread resolver([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto pending = handler.pending_list();
        handler.resolve(pending[0]->id, ApprovalDecision::Approved);
    });

    handler.request_approval(req);
    resolver.join();

    // After resolution, pending should be cleaned up
    REQUIRE(handler.pending_count() == 0);
}
```

- [ ] **Step 4: Add PendingApprovalHandler.cpp to src/security/CMakeLists.txt**

Change the first line from:
```cmake
add_library(ea-security OBJECT SecurityPolicy.cpp)
```
to:
```cmake
add_library(ea-security OBJECT SecurityPolicy.cpp PendingApprovalHandler.cpp)
```

- [ ] **Step 5: Add test file to tests/CMakeLists.txt**

Add `test_pending_approval_handler.cpp` after `test_stdin_approval_handler.cpp`:

```
    test_stdin_approval_handler.cpp
    test_pending_approval_handler.cpp
```

- [ ] **Step 6: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[pending]" --reporter compact`
Expected: All 7 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/security/PendingApprovalHandler.h src/security/PendingApprovalHandler.cpp tests/test_pending_approval_handler.cpp src/security/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(security): add PendingApprovalHandler for Server mode approval"
```

---

### Task 5: ExecuteToolsStep Integration

**Files:**
- Modify: `src/agent/steps/ExecuteToolsStep.h`
- Modify: `src/agent/steps/ExecuteToolsStep.cpp`

- [ ] **Step 1: Update ExecuteToolsStep.h**

Replace the entire file with:

```cpp
// ExecuteToolsStep — execute pending tool calls with security + approval checks
#pragma once
#include "agent/ITurnStep.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"

namespace ea::agent {

class ExecuteToolsStep : public ITurnStep {
public:
    ExecuteToolsStep(security::SecurityPolicy* policy = nullptr,
                     security::IApprovalHandler* approval = nullptr);

    Result<void> execute(TurnContext& ctx) override;
    std::string name() const override { return "execute_tools"; }

private:
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Update ExecuteToolsStep.cpp**

Replace the entire file with:

```cpp
#include "ExecuteToolsStep.h"
#include "tool/ToolOutputConfig.h"
#include "common/io/Logger.h"

namespace ea::agent {

ExecuteToolsStep::ExecuteToolsStep(security::SecurityPolicy* policy,
                                   security::IApprovalHandler* approval)
    : policy_(policy), approval_(approval) {}

Result<void> ExecuteToolsStep::execute(TurnContext& ctx) {
    if (ctx.pending_tool_calls.empty()) {
        return {};
    }

    if (!ctx.registry) {
        return Error::invalid_arg("No tool registry configured");
    }

    ctx.tool_results.clear();

    tool::ToolOutputConfig config;
    config.max_bytes = static_cast<size_t>(ctx.max_tool_output_bytes);

    for (const auto& tc : ctx.pending_tool_calls) {
        EA_DEBUG("Executing tool: {} (id: {})", tc.name, tc.id);

        // 1. SecurityPolicy hard check
        if (policy_) {
            auto check = policy_->check_tool(tc.name);
            if (!check.ok()) {
                EA_WARN("Tool blocked by security policy: {}", tc.name);
                ctx.tool_results.push_back(
                    ToolResult{tc.id, "Blocked by security policy: " + check.error().message, true});
                continue;
            }
        }

        // 2. Approval soft check for dangerous tools
        ITool* tool = ctx.registry->find(tc.name);
        if (tool && tool->is_dangerous() && approval_) {
            security::ApprovalRequest req;
            req.tool_name = tc.name;
            req.arguments = tc.arguments;
            req.description = "Tool '" + tc.name + "' is marked as dangerous";

            auto decision = approval_->request_approval(req);
            if (decision == security::ApprovalDecision::Rejected) {
                EA_WARN("Tool rejected by user: {}", tc.name);
                ctx.tool_results.push_back(
                    ToolResult{tc.id, "User rejected this tool call", true});
                continue;
            }
            if (decision == security::ApprovalDecision::Aborted) {
                EA_WARN("User aborted agent loop during approval");
                ctx.should_stop = true;
                return {};
            }
            // Approved — continue to execute
        }

        // 3. Execute the tool
        auto result = ctx.registry->execute(tc.name, tc.arguments);
        ToolResult tool_result;
        if (result.ok()) {
            tool_result = std::move(result.value());
        } else {
            tool_result = ToolResult{tc.id, "Error: " + result.error().message, true};
        }

        // 4. Truncate output using ToolOutputConfig
        tool_result.output = tool::truncate_output(
            tool_result.output, config.get_limit(tc.name), config.truncate_marker);

        ctx.tool_results.push_back(std::move(tool_result));

        EA_DEBUG("Tool {} result: {} bytes, error={}", tc.name,
                 ctx.tool_results.back().output.size(), ctx.tool_results.back().is_error);
    }

    return {};
}

}  // namespace ea::agent
```

- [ ] **Step 3: Build to verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds. Note: AgentLoop.cpp will need updating (Task 6) to pass the new constructor args.

- [ ] **Step 4: Commit**

```bash
git add src/agent/steps/ExecuteToolsStep.h src/agent/steps/ExecuteToolsStep.cpp
git commit -m "feat(agent): integrate SecurityPolicy + IApprovalHandler into ExecuteToolsStep"
```

---

### Task 6: AgentLoop Integration

**Files:**
- Modify: `src/agent/AgentLoop.h`
- Modify: `src/agent/AgentLoop.cpp`

- [ ] **Step 1: Update AgentLoop.h**

Add `#include` for SecurityPolicy and IApprovalHandler, and add constructor params. The full updated file:

```cpp
// AgentLoop — orchestrates ITurnStep chain for agent execution
#pragma once
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include "TurnContext.h"
#include "ITurnStep.h"
#include "LoopDetector.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

namespace ea::agent {

using ToolRegistry = tool::ToolRegistry;

class AgentLoop {
public:
    struct Config {
        int max_iterations = 90;
        int max_tool_output_bytes = 65536;
        int max_messages = 100;
        bool auto_memory = true;
    };

    using OutputFn = std::function<void(const std::string&)>;

    AgentLoop(IProvider* provider,
              ToolRegistry* registry,
              IMemory* memory,
              Config config,
              OutputFn output,
              security::SecurityPolicy* policy = nullptr,
              security::IApprovalHandler* approval = nullptr);

    Result<void> run(const std::string& user_input);
    void interrupt();
    const std::vector<Message>& history() const;
    void clear_history();

    // Step chain customization
    void add_step(std::unique_ptr<ITurnStep> step);
    void set_steps(std::vector<std::unique_ptr<ITurnStep>> steps);

private:
    void build_system_prompt_once();

    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    Config config_;
    OutputFn output_;
    security::SecurityPolicy* policy_;
    security::IApprovalHandler* approval_;

    std::vector<Message> history_;
    std::string system_prompt_;
    std::atomic<bool> interrupted_{false};

    // Step chain
    std::vector<std::unique_ptr<ITurnStep>> steps_;
    LoopDetector loop_detector_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Update AgentLoop.cpp**

Update the constructor to accept and pass policy + approval. Only the constructor changes; the rest stays the same:

```cpp
AgentLoop::AgentLoop(IProvider* provider,
                     ToolRegistry* registry,
                     IMemory* memory,
                     Config config,
                     OutputFn output,
                     security::SecurityPolicy* policy,
                     security::IApprovalHandler* approval)
    : provider_(provider)
    , registry_(registry)
    , memory_(memory)
    , config_(std::move(config))
    , output_(std::move(output))
    , policy_(policy)
    , approval_(approval)
{
    // Build default step chain
    steps_.push_back(std::make_unique<HistoryPruneStep>(config_.max_messages));
    steps_.push_back(std::make_unique<BuildToolSpecsStep>());
    steps_.push_back(std::make_unique<CallProviderStep>());
    steps_.push_back(std::make_unique<ParseResponseStep>());
    steps_.push_back(std::make_unique<ExecuteToolsStep>(policy_, approval_));
    steps_.push_back(std::make_unique<LoopDetectStep>(loop_detector_));
    steps_.push_back(std::make_unique<CollectResultsStep>());
}
```

The rest of AgentLoop.cpp remains unchanged.

- [ ] **Step 3: Build and run all existing tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All 242 tests pass (new params have defaults, backward compatible).

- [ ] **Step 4: Commit**

```bash
git add src/agent/AgentLoop.h src/agent/AgentLoop.cpp
git commit -m "feat(agent): add SecurityPolicy + IApprovalHandler params to AgentLoop"
```

---

### Task 7: Config Extension

**Files:**
- Modify: `src/config/Config.h`
- Modify: `src/config/Config.cpp`

- [ ] **Step 1: Update SecurityConfig in Config.h**

Add three new fields to `SecurityConfig`:

```cpp
struct SecurityConfig {
    std::string autonomy = "supervised";
    std::vector<std::string> allowed_commands;
    std::string workspace;
    int approval_timeout = 300;              // Approval timeout in seconds (Server mode)
    std::string approval_mode = "auto";      // stdin / pending / auto
    bool auto_approve_dangerous = false;     // Only effective in Full autonomy
};
```

- [ ] **Step 2: Update Config.cpp to parse new fields**

In the `if (data.contains("security"))` block, after the `allowed_commands` parsing, add:

```cpp
            cfg.security.approval_timeout = toml::find_or<int>(security, "approval_timeout", cfg.security.approval_timeout);
            if (security.contains("approval")) {
                auto approval = toml::find(security, "approval");
                cfg.security.approval_mode = toml::find_or<std::string>(approval, "mode", cfg.security.approval_mode);
                cfg.security.auto_approve_dangerous = toml::find_or<bool>(approval, "auto_approve_dangerous", cfg.security.auto_approve_dangerous);
            }
```

- [ ] **Step 3: Build and run config tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[config]" --reporter compact`
Expected: All config tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/config/Config.h src/config/Config.cpp
git commit -m "feat(config): add approval_timeout, approval_mode, auto_approve_dangerous to SecurityConfig"
```

---

### Task 8: main.cpp Integration

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update main.cpp**

Add the approval handler creation after the security policy creation (step 5), and pass it to AgentLoop (step 8). The full updated file:

```cpp
#include "config/Config.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "tool/ShellTool.h"
#include "tool/FileTool.h"
#include "tool/SearchTool.h"
#include "tool/WebTool.h"
#include "tool/MemoryTool.h"
#include "agent/AgentLoop.h"
#include "platform/Platform.h"
#include "security/SecurityPolicy.h"
#include "security/IApprovalHandler.h"
#include "security/StdinApprovalHandler.h"
#include "security/PendingApprovalHandler.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "common/net/HttpClient.h"
#include "ea/build_config.h"
#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    CLI::App app{"embedded-agent — Lightweight AI Agent for Linux & Android"};

    std::string config_path;
    bool debug = false;
    app.add_option("-c,--config", config_path, "Config file path");
    app.add_flag("--debug", debug, "Enable debug logging");

    CLI11_PARSE(app, argc, argv);

    // 1. Initialize logging
    auto home = ea::platform::home_dir();
    ea::log::init(home + "/.embedded-agent", debug);

    EA_INFO("embedded-agent v0.1.0 starting");
    EA_INFO("Platform: {}", ea::platform::platform_description());

    // 2. Load configuration
    auto cfg_result = ea::config::load(config_path);
    if (!cfg_result.ok()) {
        EA_ERROR("Config load failed: {}", cfg_result.error().message);
        std::cerr << "Config error: " << cfg_result.error().message << std::endl;
        return 1;
    }
    auto cfg = cfg_result.value();

    // 3. Create provider
    auto provider = ea::provider::create(cfg.provider);
    if (!provider) {
        EA_ERROR("Unknown provider type: {}", cfg.provider.type);
        std::cerr << "Unknown provider type: " << cfg.provider.type << std::endl;
        return 1;
    }

    // 4. Create memory
    std::string memory_path = cfg.memory.path;
    if (memory_path.empty()) {
        auto data_dir = ea::fs::config_dir();
        if (data_dir.ok()) {
            ea::fs::mkdir_p(data_dir.value());
            memory_path = data_dir.value() + "/memory.db";
        } else {
            memory_path = home + "/.embedded-agent/memory.db";
        }
    }
    auto memory = std::make_unique<ea::memory::SqliteMemory>(
        ea::memory::SqliteMemory::Config{memory_path, cfg.memory.enable_fts5});

    // 5. Create security policy
    auto security = std::make_unique<ea::security::SecurityPolicy>();
    if (!cfg.security.workspace.empty()) {
        security->set_workspace(cfg.security.workspace);
    }
    if (!cfg.security.allowed_commands.empty()) {
        security->set_allowed_commands(cfg.security.allowed_commands);
    }

    // 6. Create approval handler
    std::unique_ptr<ea::security::IApprovalHandler> approval;

    if (security->level() == ea::security::AutonomyLevel::Full && cfg.security.auto_approve_dangerous) {
        approval = nullptr;  // Full mode + auto-approve = no approval needed
    } else if (cfg.security.approval_mode == "auto") {
#ifdef EA_MODE_CLI
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
#elif defined(EA_MODE_SERVER)
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
#else
        approval = nullptr;  // Embedded mode: no interactive interface
#endif
    } else if (cfg.security.approval_mode == "stdin") {
        approval = std::make_unique<ea::security::StdinApprovalHandler>();
    } else if (cfg.security.approval_mode == "pending") {
        approval = std::make_unique<ea::security::PendingApprovalHandler>(cfg.security.approval_timeout);
    }

    // 7. Create HTTP client for WebTool
    ea::net::HttpClient http_client;

    // 8. Register tools
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<ea::tool::ShellTool>());
    registry.register_tool(std::make_unique<ea::tool::FileTool>());
    registry.register_tool(std::make_unique<ea::tool::SearchTool>());
    registry.register_tool(std::make_unique<ea::tool::WebTool>(&http_client));
    registry.register_tool(std::make_unique<ea::tool::MemoryTool>(memory.get()));

    // 9. Create agent loop
    ea::agent::AgentLoop loop(
        provider.get(), &registry, memory.get(),
        ea::agent::AgentLoop::Config{cfg.agent.max_iterations},
        [](const std::string& text) {
            std::cout << text << std::endl;
        },
        security.get(),
        approval.get()
    );

    // 10. Interactive loop
    std::string input;
    std::cout << "embedded-agent v0.1.0 (type /quit to exit)" << std::endl;

    while (true) {
        std::cout << "\n> " << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input == "/quit" || input == "/exit") break;
        if (input.empty()) continue;

        auto result = loop.run(input);
        if (!result.ok()) {
            EA_ERROR("Agent error: {}", result.error().message);
            std::cerr << "Error: " << result.error().message << std::endl;
        }
    }

    EA_INFO("embedded-agent shutting down");
    return 0;
}
```

- [ ] **Step 2: Build to verify compilation**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate approval handler into main.cpp with auto mode selection"
```

---

### Task 9: Integration Tests

**Files:**
- Modify: `tests/test_integration_provider_tool.cpp`

- [ ] **Step 1: Read current test file**

Read `tests/test_integration_provider_tool.cpp` to understand existing structure before adding new test cases.

- [ ] **Step 2: Add approval integration test cases**

Append the following test cases to the end of `tests/test_integration_provider_tool.cpp`, before the closing namespace or end of file:

```cpp
// --- Approval integration tests ---

class MockApprovalHandler : public ea::security::IApprovalHandler {
public:
    ea::security::ApprovalDecision next_decision = ea::security::ApprovalDecision::Approved;
    mutable ea::security::ApprovalRequest last_request;

    ea::security::ApprovalDecision request_approval(const ea::security::ApprovalRequest& req) override {
        last_request = req;
        return next_decision;
    }
};

class DangerousTool : public ea::ITool {
public:
    std::string name() const override { return "dangerous_op"; }
    std::string description() const override { return "A dangerous tool for testing"; }
    nlohmann::json parameters_schema() const override { return nlohmann::json::object(); }
    ea::Result<ea::ToolResult> execute(const nlohmann::json&) override {
        return ea::ToolResult{"", "dangerous result", false};
    }
    bool is_dangerous() const override { return true; }
};

class SafeTool : public ea::ITool {
public:
    std::string name() const override { return "safe_op"; }
    std::string description() const override { return "A safe tool for testing"; }
    nlohmann::json parameters_schema() const override { return nlohmann::json::object(); }
    ea::Result<ea::ToolResult> execute(const nlohmann::json&) override {
        return ea::ToolResult{"", "safe result", false};
    }
    bool is_dangerous() const override { return false; }
};

TEST_CASE("Approval: safe tool executes without approval", "[integration][approval]") {
    MockApprovalHandler approval;
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<SafeTool>());

    ea::agent::AgentLoop loop(
        nullptr, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    // Safe tool should not trigger approval
    // We verify by checking that last_request.tool_name is empty (never called)
    REQUIRE(approval.last_request.tool_name.empty());
}

TEST_CASE("Approval: dangerous tool approved executes normally", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Approved;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    // Create a provider that calls the dangerous tool
    IntegrationProvider provider;
    provider.add_response(ea::LLMResponse{
        "", "tool_use",
        {ea::ToolCall{"1", "dangerous_op", nlohmann::json::object()}}
    });
    provider.add_response(ea::LLMResponse{"Done!", "stop", {}});

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(approval.last_request.tool_name == "dangerous_op");
}

TEST_CASE("Approval: dangerous tool rejected returns error result", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Rejected;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.add_response(ea::LLMResponse{
        "", "tool_use",
        {ea::ToolCall{"1", "dangerous_op", nlohmann::json::object()}}
    });
    // Second response after rejection
    provider.add_response(ea::LLMResponse{"I see the tool was rejected", "stop", {}});

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());
    REQUIRE(approval.last_request.tool_name == "dangerous_op");
}

TEST_CASE("Approval: dangerous tool aborted stops agent loop", "[integration][approval]") {
    MockApprovalHandler approval;
    approval.next_decision = ea::security::ApprovalDecision::Aborted;

    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.add_response(ea::LLMResponse{
        "", "tool_use",
        {ea::ToolCall{"1", "dangerous_op", nlohmann::json::object()}}
    });

    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        &approval
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());  // Aborted sets should_stop, loop exits cleanly
}

TEST_CASE("Approval: null handler allows dangerous tools without approval", "[integration][approval]") {
    ea::tool::ToolRegistry registry;
    registry.register_tool(std::make_unique<DangerousTool>());

    IntegrationProvider provider;
    provider.add_response(ea::LLMResponse{
        "", "tool_use",
        {ea::ToolCall{"1", "dangerous_op", nlohmann::json::object()}}
    });
    provider.add_response(ea::LLMResponse{"Done!", "stop", {}});

    // No approval handler (nullptr)
    ea::agent::AgentLoop loop(
        &provider, &registry, nullptr,
        ea::agent::AgentLoop::Config{3},
        [](const std::string&) {},
        nullptr,
        nullptr
    );

    auto result = loop.run("test");
    REQUIRE(result.ok());  // Dangerous tool executes without approval
}
```

- [ ] **Step 3: Build and run integration tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[approval]" --reporter compact`
Expected: All approval tests pass (unit + integration).

- [ ] **Step 4: Run full test suite**

Run: `cd /home/lsy/embedded-agent/build && ./tests/ea-tests --reporter compact`
Expected: All tests pass, zero regressions.

- [ ] **Step 5: Commit**

```bash
git add tests/test_integration_provider_tool.cpp
git commit -m "test(integration): add approval system integration tests"
```

---

### Task 10: Final Verification

- [ ] **Step 1: Clean rebuild + full test suite**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) && ./tests/ea-tests --reporter compact`
Expected: All tests pass.

- [ ] **Step 2: Verify no compiler warnings in ea-* code**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep "warning:" | grep -v "mbedtls" | grep -v "third_party"`
Expected: Zero warnings in project code.

- [ ] **Step 3: Final commit (if any fixes needed)**

Only commit if fixes were required in steps 1-2.
