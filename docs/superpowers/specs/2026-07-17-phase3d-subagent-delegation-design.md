# Phase 3D: Subagent Delegation Design

**Date:** 2026-07-17
**Status:** Approved
**Scope:** Full multi-agent system with LLM-driven delegation, config templates, and parallel execution

## Overview

Add a subagent delegation system where the main agent can delegate subtasks to independent `AgentLoop` instances. Subagents are created from config templates, each with their own system prompt, tool access, and memory scope. The main agent delegates via a `DelegateTool` — an ITool that lets the LLM invoke subagents naturally. Parallel execution is supported for independent subtasks.

**Design approach:** Config-driven templates → SubagentOrchestrator (create/schedule/collect) → Subagent (independent AgentLoop) → DelegateTool (ITool bridge). The orchestrator manages the lifecycle; the main agent interacts solely through tool calls.

---

## 1. SubagentConfig

```cpp
// src/agent/SubagentConfig.h
#pragma once
#include <string>
#include <vector>

namespace ea::agent {

struct SubagentConfig {
    std::string name;                    // "researcher", "coder", "reviewer"
    std::string description;             // Description shown to main agent
    std::string model;                   // Model override (empty = inherit from main)
    std::string system_prompt;           // Subagent's system prompt
    std::vector<std::string> toolsets;   // Accessible Toolset names
    bool shared_memory = true;           // Share main agent's Memory
    int max_iterations = 20;             // Max iterations per delegation
    bool dangerous = false;              // Requires approval to delegate
};

}  // namespace ea::agent
```

---

## 2. Subagent

Each subagent wraps an independent `AgentLoop` with its own configuration.

```cpp
// src/agent/Subagent.h
#pragma once
#include "SubagentConfig.h"
#include "core/IProvider.h"
#include "tool/ToolRegistry.h"
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <atomic>
#include <memory>
#include <string>

namespace ea::agent {

class Subagent {
public:
    Subagent(std::string id, SubagentConfig config,
             IProvider* provider, ToolRegistry* registry, IMemory* memory);

    // Execute a subtask, return the final text output
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

**Implementation notes:**
- `execute()` creates a new `AgentLoop` per invocation with the subagent's config
- The AgentLoop is created fresh each time (no state carried between delegations)
- `running_` flag prevents concurrent execution of the same subagent instance
- Output is collected via a string callback passed to the AgentLoop

---

## 3. SubagentOrchestrator

Manages template registration, subagent creation, and parallel execution.

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

    // Register a subagent template
    void register_template(SubagentConfig config);

    // Create a subagent instance from a template
    Result<std::shared_ptr<Subagent>> create(const std::string& template_name);

    // Execute a single subtask
    Result<std::string> delegate(const std::string& template_name,
                                 const std::string& task);

    // Execute multiple subtasks in parallel
    // Returns vector of (template_name, result_string) pairs
    Result<std::vector<std::pair<std::string, std::string>>>
    delegate_parallel(const std::vector<std::pair<std::string, std::string>>& tasks);

    // List available template names
    std::vector<std::string> available_templates() const;

    // Check if a template exists
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

**delegate_parallel implementation:**

```cpp
Result<std::vector<std::pair<std::string, std::string>>>
SubagentOrchestrator::delegate_parallel(
    const std::vector<std::pair<std::string, std::string>>& tasks)
{
    std::vector<std::pair<std::string, std::string>> results;
    results.resize(tasks.size());

    std::vector<std::thread> threads;
    std::mutex results_mutex;

    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& [template_name, task] = tasks[i];
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
```

---

## 4. DelegateTool

ITool that lets the main agent delegate subtasks via tool calls.

```cpp
// src/agent/DelegateTool.h
#pragma once
#include "core/ITool.h"
#include "SubagentOrchestrator.h"
#include <string>

namespace ea::agent {

class DelegateTool : public ITool {
public:
    explicit DelegateTool(SubagentOrchestrator* orchestrator);

    std::string name() const override { return "delegate"; }

    std::string description() const override {
        return "Delegate a subtask to a specialized sub-agent. "
               "Use for tasks that benefit from a different perspective, "
               "expertise, or independent execution.";
    }

    json parameters_schema() const override;

    Result<ToolResult> execute(const json& args) override;

    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    SubagentOrchestrator* orchestrator_;
};

}  // namespace ea::agent
```

**parameters_schema:**

```json
{
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
}
```

**execute logic:**

```cpp
Result<ToolResult> DelegateTool::execute(const json& args) {
    bool parallel = args.value("parallel", false);

    if (parallel && args.contains("tasks")) {
        // Parallel mode
        std::vector<std::pair<std::string, std::string>> tasks;
        for (const auto& t : args["tasks"]) {
            tasks.push_back({t.value("agent", ""), t.value("task", "")});
        }
        auto result = orchestrator_->delegate_parallel(tasks);
        if (!result.ok()) return result.error();

        // Format results
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
```

---

## 5. Configuration

### TOML Config

```toml
[[agent.subagents]]
name = "researcher"
description = "Research a topic and return findings"
model = ""
system_prompt = "You are a research assistant. Find and summarize information concisely."
toolsets = ["core", "search"]
shared_memory = true
max_iterations = 20
dangerous = false

[[agent.subagents]]
name = "coder"
description = "Write and execute code to solve a problem"
system_prompt = "You are a coding assistant. Write and test code."
toolsets = ["core", "file"]
shared_memory = true
max_iterations = 30
dangerous = true
```

### Config Struct Extension

```cpp
struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
    bool compression_enable = true;
    int compression_max_tokens = 8000;
    int compression_keep_recent_turns = 4;
    // Subagent templates
    std::vector<SubagentConfig> subagents;
};
```

### Config Parsing

```cpp
if (agent.contains("subagents")) {
    auto subs = toml::find<std::vector<toml::value>>(agent, "subagents");
    for (const auto& sub_val : subs) {
        auto sub = sub_val.as_table();
        SubagentConfig sc;
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

---

## 6. main.cpp Integration

```cpp
// After tool registration and MCP setup, before AgentLoop creation:

// 11. Create subagent orchestrator
auto orchestrator = std::make_unique<ea::agent::SubagentOrchestrator>(
    provider.get(), &registry, memory.get());

for (auto& sub_cfg : cfg.agent.subagents) {
    EA_INFO("Registering subagent template: {}", sub_cfg.name);
    orchestrator->register_template(sub_cfg);
}

// Register DelegateTool if any templates exist
if (!cfg.agent.subagents.empty()) {
    registry.register_tool(std::make_unique<ea::agent::DelegateTool>(orchestrator.get()));
}
```

---

## 7. Memory Isolation

When `shared_memory = true`, the subagent uses the main agent's Memory through a `ScopedMemory` wrapper with the subagent's ID as scope. This provides:

- **Write isolation**: Each subagent's stores are prefixed with `agent_id:`
- **Read isolation**: Subagents read their own + allowlisted entries
- **System prompt injection**: Subagent sees relevant memories in its system prompt

When `shared_memory = false`, the subagent uses a `NullMemory` backend (no persistence).

---

## 8. Test Strategy

### Unit Tests

| Test File | Coverage |
|-----------|----------|
| `tests/test_subagent.cpp` | SubagentConfig, SubagentOrchestrator, DelegateTool |

### Key Test Cases

1. SubagentOrchestrator registers templates and lists them
2. SubagentOrchestrator creates subagent from template
3. SubagentOrchestrator delegate executes subtask via MockProvider
4. SubagentOrchestrator delegate returns error for unknown template
5. DelegateTool single delegation maps to orchestrator
6. DelegateTool parallel delegation maps to delegate_parallel
7. DelegateTool returns error on missing agent name
8. DelegateTool is_dangerous returns true
9. SubagentConfig default values

### Integration with MockProvider

Create a MockProvider that returns predefined responses for the subagent's AgentLoop, verifying end-to-end delegation flow.

---

## 9. File Manifest

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

## Scope Summary

| Dimension | Content |
|-----------|---------|
| Goal | Full multi-agent system with delegation |
| Delegation | LLM-driven via DelegateTool |
| Templates | Config-driven SubagentConfig |
| Execution | Serial (delegate) + Parallel (delegate_parallel) |
| Memory | ScopedMemory with agent_id isolation |
| Approval | dangerous flag triggers approval |
| Integration | DelegateTool registered as ITool in ToolRegistry |

**Not in scope:** Subagent-to-subagent communication, dynamic subagent creation by LLM, subagent result streaming, persistent subagent state across delegations.
