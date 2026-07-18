# Phase 3D: Subagent Delegation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a subagent delegation system with config-driven templates, serial/parallel execution, and DelegateTool integration.

**Architecture:** SubagentConfig (templates) → SubagentOrchestrator (create/schedule/collect) → Subagent (independent AgentLoop) → DelegateTool (ITool bridge). Main agent delegates via tool calls.

**Tech Stack:** C++17, CMake, nlohmann/json, spdlog, Catch2

---

## File Structure

### New Files

| File | Responsibility |
|------|---------------|
| `src/agent/SubagentConfig.h` | Subagent template config struct |
| `src/agent/Subagent.h` | Subagent declaration |
| `src/agent/Subagent.cpp` | Subagent execution implementation |
| `src/agent/SubagentOrchestrator.h` | Orchestrator declaration |
| `src/agent/SubagentOrchestrator.cpp` | Orchestrator implementation |
| `src/agent/DelegateTool.h` | ITool bridge for delegation (header-only) |
| `tests/test_subagent.cpp` | Subagent + orchestrator + DelegateTool tests |

### Modified Files

| File | Change |
|------|--------|
| `src/config/Config.h` | AgentConfig adds subagents vector |
| `src/config/Config.cpp` | Parse [[agent.subagents]] TOML array |
| `src/agent/CMakeLists.txt` | Add Subagent.cpp, SubagentOrchestrator.cpp |
| `src/main.cpp` | Create orchestrator, register templates, add DelegateTool |
| `tests/CMakeLists.txt` | Add test_subagent.cpp |

---

### Task 1: SubagentConfig + Subagent

**Files:**
- Create: `src/agent/SubagentConfig.h`
- Create: `src/agent/Subagent.h`
- Create: `src/agent/Subagent.cpp`

- [ ] **Step 1: Write SubagentConfig.h**

```cpp
// src/agent/SubagentConfig.h
#pragma once
#include <string>
#include <vector>

namespace ea::agent {

struct SubagentConfig {
    std::string name;
    std::string description;
    std::string model;
    std::string system_prompt;
    std::vector<std::string> toolsets;
    bool shared_memory = true;
    int max_iterations = 20;
    bool dangerous = false;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Write Subagent.h**

```cpp
// src/agent/Subagent.h
#pragma once
#include "SubagentConfig.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <atomic>
#include <string>

namespace ea::agent {

class Subagent {
public:
    Subagent(std::string id, SubagentConfig config,
             IProvider* provider, ToolRegistry* registry, IMemory* memory);

    Result<std::string> execute(const std::string& task);

    const std::string& id() const { return id_; }
    const SubagentConfig& config() const { return config_; }
    bool is_running() const { return running_.load(); }

private:
    std::string id_;
    SubagentConfig config_;
    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    std::atomic<bool> running_{false};
};

}  // namespace ea::agent
```

- [ ] **Step 3: Write Subagent.cpp**

```cpp
// src/agent/Subagent.cpp
#include "Subagent.h"
#include "AgentLoop.h"
#include "common/io/Logger.h"

namespace ea::agent {

Subagent::Subagent(std::string id, SubagentConfig config,
                   IProvider* provider, ToolRegistry* registry, IMemory* memory)
    : id_(std::move(id))
    , config_(std::move(config))
    , provider_(provider)
    , registry_(registry)
    , memory_(memory) {}

Result<std::string> Subagent::execute(const std::string& task) {
    if (running_.exchange(true)) {
        return Error::invalid_arg("Subagent '" + id_ + "' is already running");
    }

    EA_INFO("Subagent '{}' executing task: {}", id_, task.substr(0, 100));

    std::string output;
    AgentLoop loop(
        provider_, registry_, memory_,
        AgentLoop::Config{config_.max_iterations},
        [&](const std::string& text) { output = text; }
    );

    auto result = loop.run(task);

    running_ = false;

    if (!result.ok()) {
        EA_WARN("Subagent '{}' failed: {}", id_, result.error().message);
        return result.error();
    }

    EA_INFO("Subagent '{}' completed", id_);
    return output;
}

}  // namespace ea::agent
```

- [ ] **Step 4: Commit**

```bash
git add src/agent/SubagentConfig.h src/agent/Subagent.h src/agent/Subagent.cpp
git commit -m "feat(agent): add SubagentConfig and Subagent with independent AgentLoop execution"
```

---

### Task 2: SubagentOrchestrator

**Files:**
- Create: `src/agent/SubagentOrchestrator.h`
- Create: `src/agent/SubagentOrchestrator.cpp`

- [ ] **Step 1: Write SubagentOrchestrator.h**

```cpp
// src/agent/SubagentOrchestrator.h
#pragma once
#include "SubagentConfig.h"
#include "Subagent.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ea::agent {

class SubagentOrchestrator {
public:
    explicit SubagentOrchestrator(IProvider* provider,
                                   ToolRegistry* registry,
                                   IMemory* memory);

    void register_template(SubagentConfig config);

    Result<std::shared_ptr<Subagent>> create(const std::string& template_name);

    Result<std::string> delegate(const std::string& template_name,
                                 const std::string& task);

    Result<std::vector<std::pair<std::string, std::string>>>
    delegate_parallel(const std::vector<std::pair<std::string, std::string>>& tasks);

    std::vector<std::string> available_templates() const;
    bool has_template(const std::string& name) const;

private:
    IProvider* provider_;
    ToolRegistry* registry_;
    IMemory* memory_;
    std::map<std::string, SubagentConfig> templates_;
    int next_id_ = 1;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Write SubagentOrchestrator.cpp**

```cpp
// src/agent/SubagentOrchestrator.cpp
#include "SubagentOrchestrator.h"
#include "common/io/Logger.h"
#include <thread>
#include <mutex>

namespace ea::agent {

SubagentOrchestrator::SubagentOrchestrator(IProvider* provider,
                                           ToolRegistry* registry,
                                           IMemory* memory)
    : provider_(provider), registry_(registry), memory_(memory) {}

void SubagentOrchestrator::register_template(SubagentConfig config) {
    EA_INFO("Registered subagent template: {}", config.name);
    templates_[config.name] = std::move(config);
}

Result<std::shared_ptr<Subagent>> SubagentOrchestrator::create(const std::string& template_name) {
    auto it = templates_.find(template_name);
    if (it == templates_.end()) {
        return Error::not_found("Unknown subagent template: " + template_name);
    }

    std::string id = template_name + "-" + std::to_string(next_id_++);
    auto subagent = std::make_shared<Subagent>(
        std::move(id), it->second, provider_, registry_, memory_);

    return subagent;
}

Result<std::string> SubagentOrchestrator::delegate(const std::string& template_name,
                                                    const std::string& task) {
    auto subagent_result = create(template_name);
    if (!subagent_result.ok()) return subagent_result.error();

    return subagent_result.value()->execute(task);
}

Result<std::vector<std::pair<std::string, std::string>>>
SubagentOrchestrator::delegate_parallel(
    const std::vector<std::pair<std::string, std::string>>& tasks)
{
    std::vector<std::pair<std::string, std::string>> results;
    results.resize(tasks.size());

    std::vector<std::thread> threads;
    std::mutex results_mutex;

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& template_name = tasks[i].first;
        const auto& task = tasks[i].second;

        threads.emplace_back([&, i, template_name, task]() {
            auto result = delegate(template_name, task);
            std::lock_guard<std::mutex> lock(results_mutex);
            results[i] = {template_name, result.ok() ? result.value() : result.error().message};
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return results;
}

std::vector<std::string> SubagentOrchestrator::available_templates() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : templates_) {
        names.push_back(name);
    }
    return names;
}

bool SubagentOrchestrator::has_template(const std::string& name) const {
    return templates_.count(name) > 0;
}

}  // namespace ea::agent
```

- [ ] **Step 3: Commit**

```bash
git add src/agent/SubagentOrchestrator.h src/agent/SubagentOrchestrator.cpp
git commit -m "feat(agent): add SubagentOrchestrator with serial and parallel delegation"
```

---

### Task 3: DelegateTool

**Files:**
- Create: `src/agent/DelegateTool.h`

- [ ] **Step 1: Write DelegateTool.h (header-only)**

```cpp
// src/agent/DelegateTool.h
#pragma once
#include "core/ITool.h"
#include "SubagentOrchestrator.h"

namespace ea::agent {

class DelegateTool : public ITool {
public:
    explicit DelegateTool(SubagentOrchestrator* orchestrator)
        : orchestrator_(orchestrator) {}

    std::string name() const override { return "delegate"; }

    std::string description() const override {
        return "Delegate a subtask to a specialized sub-agent. "
               "Use for tasks that benefit from a different perspective, "
               "expertise, or independent execution.";
    }

    json parameters_schema() const override {
        return json::parse(R"({
            "type": "object",
            "properties": {
                "agent": {
                    "type": "string",
                    "description": "Name of the sub-agent template to use"
                },
                "task": {
                    "type": "string",
                    "description": "Description of the task to delegate"
                },
                "parallel": {
                    "type": "boolean",
                    "description": "Execute multiple sub-tasks in parallel",
                    "default": false
                },
                "tasks": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "agent": {"type": "string"},
                            "task": {"type": "string"}
                        },
                        "required": ["agent", "task"]
                    },
                    "description": "Multiple sub-tasks for parallel execution"
                }
            },
            "required": ["agent", "task"]
        })");
    }

    Result<ToolResult> execute(const json& args) override {
        bool parallel = args.value("parallel", false);

        if (parallel && args.contains("tasks") && args["tasks"].is_array()) {
            std::vector<std::pair<std::string, std::string>> tasks;
            for (const auto& t : args["tasks"]) {
                tasks.push_back({t.value("agent", ""), t.value("task", "")});
            }

            auto result = orchestrator_->delegate_parallel(tasks);
            if (!result.ok()) {
                return ToolResult{"", "Parallel delegation failed: " + result.error().message, true};
            }

            std::string output;
            for (const auto& [name, res] : result.value()) {
                output += "## " + name + "\n" + res + "\n\n";
            }
            return ToolResult{"", output, false};
        }

        // Single delegation
        std::string agent_name = args.value("agent", "");
        std::string task = args.value("task", "");

        auto result = orchestrator_->delegate(agent_name, task);
        if (!result.ok()) {
            return ToolResult{"", "Delegation failed: " + result.error().message, true};
        }
        return ToolResult{"", result.value(), false};
    }

    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    SubagentOrchestrator* orchestrator_;
};

}  // namespace ea::agent
```

- [ ] **Step 2: Commit**

```bash
git add src/agent/DelegateTool.h
git commit -m "feat(agent): add DelegateTool for LLM-driven subagent delegation"
```

---

### Task 4: CMakeLists + Config + main.cpp

**Files:**
- Modify: `src/agent/CMakeLists.txt`
- Modify: `src/config/Config.h`
- Modify: `src/config/Config.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Update src/agent/CMakeLists.txt**

Add Subagent.cpp and SubagentOrchestrator.cpp to the OBJECT library:

```cmake
add_library(ea-agent OBJECT
    AgentLoop.cpp
    SystemPrompt.cpp
    LoopDetector.cpp
    ContextCompressor.cpp
    Subagent.cpp
    SubagentOrchestrator.cpp
    steps/CallProviderStep.cpp
    steps/LoopDetectStep.cpp
    steps/ExecuteToolsStep.cpp
)
```

- [ ] **Step 2: Update src/config/Config.h**

Add `std::vector<SubagentConfig> subagents;` to AgentConfig (after the compression fields).

Need to include SubagentConfig.h or forward-declare. Since SubagentConfig is in ea::agent namespace and Config.h is in ea::config, add the include:

```cpp
#include "agent/SubagentConfig.h"
```

And add the field to AgentConfig:

```cpp
struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
    std::vector<agent::SubagentConfig> subagents;
};
```

- [ ] **Step 3: Update src/config/Config.cpp**

After the compression parsing block, add:

```cpp
            if (agent.contains("subagents")) {
                auto subs = toml::find<std::vector<toml::value>>(agent, "subagents");
                for (const auto& sub_val : subs) {
                    auto sub = sub_val.as_table();
                    agent::SubagentConfig sc;
                    sc.name = toml::find<std::string>(sub, "name");
                    sc.description = toml::find<std::string>(sub, "description");
                    sc.model = toml::find_or<std::string>(sub, "model", "");
                    sc.system_prompt = toml::find_or<std::string>(sub, "system_prompt", "");
                    if (sub.contains("toolsets")) {
                        sc.toolsets = toml::find<std::vector<std::string>>(sub, "toolsets");
                    }
                    sc.shared_memory = toml::find_or<bool>(sub, "shared_memory", true);
                    sc.max_iterations = toml::find_or<int>(sub, "max_iterations", 20);
                    sc.dangerous = toml::find_or<bool>(sub, "dangerous", false);
                    cfg.agent.subagents.push_back(std::move(sc));
                }
            }
```

- [ ] **Step 4: Update src/main.cpp**

Add include:
```cpp
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
```

After MCP server setup (step 7.5), add step 7.6:
```cpp
    // 7.6. Create subagent orchestrator
    auto orchestrator = std::make_unique<ea::agent::SubagentOrchestrator>(
        provider.get(), &registry, memory.get());

    for (auto& sub_cfg : cfg.agent.subagents) {
        EA_INFO("Registering subagent template: {}", sub_cfg.name);
        orchestrator->register_template(sub_cfg);
    }

    if (!cfg.agent.subagents.empty()) {
        registry.register_tool(std::make_unique<ea::agent::DelegateTool>(orchestrator.get()));
    }
```

- [ ] **Step 5: Build and test**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests --reporter compact`

- [ ] **Step 6: Commit**

```bash
git add src/agent/CMakeLists.txt src/config/Config.h src/config/Config.cpp src/main.cpp
git commit -m "feat: add subagent config, CMake, and main.cpp integration"
```

---

### Task 5: Tests

**Files:**
- Create: `tests/test_subagent.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write test file**

```cpp
// tests/test_subagent.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/SubagentConfig.h"
#include "agent/SubagentOrchestrator.h"
#include "agent/DelegateTool.h"
#include "core/IProvider.h"

using namespace ea::agent;

// Mock provider for subagent testing
class MockSubProvider : public ea::IProvider {
public:
    std::string name() const override { return "mock-sub"; }
    std::vector<std::string> list_models() const override { return {}; }
    ea::provider::ProviderCapabilities capabilities() const override {
        return {true, true, false, false, false};
    }

    void set_next_response(std::string text) {
        next_text_ = std::move(text);
    }

    Result<LLMResponse> chat(const std::vector<Message>&,
                              const std::vector<ToolSpec>&,
                              const std::string&,
                              const ChatOptions&) override {
        call_count_++;
        LLMResponse resp;
        resp.content = next_text_;
        resp.stop_reason = "stop";
        return resp;
    }

    Result<void> stream_chat(const std::vector<Message>&, const std::vector<ToolSpec>&,
                              const std::string&, std::function<void(const StreamChunk&)>,
                              const ChatOptions&) override {
        return ea::Error::net("not implemented");
    }

    int call_count() const { return call_count_; }

private:
    std::string next_text_;
    int call_count_ = 0;
};

TEST_CASE("SubagentConfig default values", "[subagent]") {
    SubagentConfig config;
    REQUIRE(config.shared_memory == true);
    REQUIRE(config.max_iterations == 20);
    REQUIRE(config.dangerous == false);
    REQUIRE(config.model.empty());
    REQUIRE(config.toolsets.empty());
}

TEST_CASE("SubagentOrchestrator registers and lists templates", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "researcher";
    cfg.description = "Research assistant";
    orchestrator.register_template(cfg);

    REQUIRE(orchestrator.has_template("researcher"));
    REQUIRE_FALSE(orchestrator.has_template("coder"));

    auto templates = orchestrator.available_templates();
    REQUIRE(templates.size() == 1);
    REQUIRE(templates[0] == "researcher");
}

TEST_CASE("SubagentOrchestrator create returns error for unknown template", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    auto result = orchestrator.create("nonexistent");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SubagentOrchestrator delegate returns error for unknown template", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    auto result = orchestrator.delegate("nonexistent", "do something");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("SubagentOrchestrator delegate executes subtask", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Research result: found 3 relevant papers");

    SubagentOrchestrator orchestrator(provider.get(), nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "researcher";
    cfg.description = "Research assistant";
    cfg.system_prompt = "You are a research assistant.";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    auto result = orchestrator.delegate("researcher", "Find papers about AI safety");
    REQUIRE(result.ok());
    REQUIRE(result.value() == "Research result: found 3 relevant papers");
    REQUIRE(provider->call_count() >= 1);
}

TEST_CASE("DelegateTool single delegation", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Task completed successfully");

    SubagentOrchestrator orchestrator(provider.get(), nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    cfg.description = "Worker sub-agent";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    DelegateTool tool(&orchestrator);

    REQUIRE(tool.name() == "delegate");
    REQUIRE(tool.is_dangerous() == true);
    REQUIRE(tool.is_mutating() == true);

    nlohmann::json args = {
        {"agent", "worker"},
        {"task", "Do the thing"}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    REQUIRE(result.value().output == "Task completed successfully");
}

TEST_CASE("DelegateTool returns error for unknown agent", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);
    DelegateTool tool(&orchestrator);

    nlohmann::json args = {
        {"agent", "nonexistent"},
        {"task", "Do something"}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == true);
}

TEST_CASE("DelegateTool parallel delegation", "[subagent]") {
    auto provider = std::make_shared<MockSubProvider>();
    provider->set_next_response("Parallel result");

    SubagentOrchestrator orchestrator(provider.get(), nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    cfg.description = "Worker";
    cfg.max_iterations = 1;
    orchestrator.register_template(cfg);

    DelegateTool tool(&orchestrator);

    nlohmann::json args = {
        {"agent", "worker"},
        {"task", "placeholder"},
        {"parallel", true},
        {"tasks", nlohmann::json::array({
            {{"agent", "worker"}, {"task", "Task A"}},
            {{"agent", "worker"}, {"task", "Task B"}}
        })}
    };

    auto result = tool.execute(args);
    REQUIRE(result.ok());
    REQUIRE(result.value().is_error == false);
    // Output should contain results from both tasks
    REQUIRE(result.value().output.find("worker") != std::string::npos);
}

TEST_CASE("SubagentOrchestrator create generates unique IDs", "[subagent]") {
    SubagentOrchestrator orchestrator(nullptr, nullptr, nullptr);

    SubagentConfig cfg;
    cfg.name = "worker";
    orchestrator.register_template(cfg);

    auto s1 = orchestrator.create("worker");
    auto s2 = orchestrator.create("worker");

    REQUIRE(s1.ok());
    REQUIRE(s2.ok());
    REQUIRE(s1.value()->id() != s2.value()->id());
}
```

- [ ] **Step 2: Add test file to tests/CMakeLists.txt**

Add `test_subagent.cpp` to the source list.

- [ ] **Step 3: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) && ./tests/ea-tests "[subagent]" --reporter compact`

- [ ] **Step 4: Run full test suite**

Run: `./tests/ea-tests --reporter compact`

- [ ] **Step 5: Commit**

```bash
git add tests/test_subagent.cpp tests/CMakeLists.txt
git commit -m "test(agent): add subagent delegation tests"
```

---

### Task 6: Final Verification

- [ ] **Step 1: Clean rebuild + full test suite**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . --clean-first -j$(nproc) && ./tests/ea-tests --reporter compact`

- [ ] **Step 2: Verify no compiler warnings**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . -j$(nproc) 2>&1 | grep "warning:" | grep -v "mbedtls" | grep -v "third_party"`
