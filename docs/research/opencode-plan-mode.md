# OpenCode Plan Mode 实现调研

调研对象：本机源码 `/home/lsy/opencode-dev`（opencode，AI 编码 agent）。

## 结论

OpenCode 的 plan mode **不是一个独立的运行时状态机，而是 `plan` agent**。它复用同一套 agent/session/tool 管线，靠三样东西实现"只规划、不执行"：

1. **per-agent 权限规则**：`edit` 默认 deny，只有 plan 文件路径 allow；
2. **注入的系统提示词**：plan agent 每轮在最后一条 user 消息里附加 plan-mode 提醒，规定五阶段工作流；
3. **plan_exit 工具**：计划完成后向用户提问，批准后注入一条 `agent: "build"` 的合成消息，完成 plan → build 交接。

进入 plan mode 就是切换 agent（TUI 里 Tab 键，CLI 里 `--agent plan`），退出/交接则走 `plan_exit` 工具。

## 关键实现位置

### 1. `plan` agent 定义与权限

文件：`packages/opencode/src/agent/agent.ts`

- `build` agent 允许 `plan_enter`，`plan` agent 允许 `plan_exit`（约 143-166 行）。
- `plan` agent 的权限（约 157-178 行）：
  - `edit: { "*": "deny", ".opencode/plans/*.md": "allow", <全局 plans 目录相对路径>/*.md: "allow" }`；
  - `task.general: "deny"`（不能调用 general subagent），`task.explore` 仍 allow；
  - `question: "allow"`（可以向用户提问）；
  - `external_directory` 只放行 plans 目录。

核心插件里有同样一份 V2 定义：`packages/core/src/plugin/agent.ts`（约 107-152 行）。

对应测试：`packages/opencode/test/agent/agent.test.ts`（约 72-89 行）断言 plan agent 对 `edit` 是 deny、对 `.opencode/plans/foo.md` 是 allow、对 `task general` 是 deny。

### 2. 工具级执行前的权限过滤

文件：`packages/opencode/src/session/llm/request.ts`

- `resolveTools()`（约 208-217 行）用 `Permission.disabled()` 过滤掉整体被 deny 的工具。
- 文件：`packages/opencode/src/permission/index.ts`
  - `evaluate()`（约 45-53 行）按规则列表 `findLast` 匹配，默认未匹配是 `ask`；
  - `disabled()`（约 60-73 行）只在"permission 匹配 + pattern 为 `*` + deny"时整体隐藏工具。plan agent 的 edit 规则里混有 allow 的 `*.md`，所以 `edit/write` 工具仍可见，真正拦在资源级。
- 各工具执行时再调 `permission.assert`，例如 `packages/core/src/tool/edit.ts`、`write.ts`、`bash.ts`。

### 3. plan-mode 系统提示词注入

文件：`packages/opencode/src/session/reminders.ts`（`SessionReminders.apply`）

- 非实验模式（`flags.experimentalPlanMode` 为 false）：
  - 当前 agent 是 `plan` 时，把 `packages/opencode/src/session/prompt/plan.txt` 追加到最后一条 user 消息（约 25-31 行）；
  - 之前是 plan、现在切回 build 时，注入 `build-switch.txt`（约 32-37 行）。
- 实验模式（`OPENCODE_EXPERIMENTAL_PLAN_MODE=true`）：
  - plan agent 首次进入时创建 plan 文件目录，并注入 `packages/opencode/src/session/prompt/plan-mode.txt`，用 `${planInfo}` 告诉模型 plan 文件路径（约 65-84 行）；
  - 切回 build 时，若 plan 文件存在，注入 `build-switch.txt` + "A plan file exists at ... You should execute on the plan"（约 42-58 行）。
- 调用点在 `packages/opencode/src/session/prompt.ts`（约 1180 行），每轮构建消息时执行。

`plan-mode.txt` 规定五阶段：探索（并行最多 3 个 explore subagent）→ 设计 → 审查 → 写最终 plan 文件 → 调用 `plan_exit`。文件里明确"除了 plan 文件外只能做只读操作"。

注意：`plan-mode.txt` 的 Phase 2 写的是 "Launch general agent(s) to design..."，但 plan agent 默认权限 `task.general: "deny"`（`agent.test.ts` 也断言了这一点），源码里存在这个提示词与权限的不一致；Phase 1 的 explore subagent 是被允许的。

### 4. plan 文件路径

文件：`packages/opencode/src/session/session.ts`（`Session.plan`，约 331-337 行）

```
有 VCS 的项目：<worktree>/.opencode/plans/<创建时间>-<session slug>.md
非 VCS 项目：  <Global.Path.data>/plans/<创建时间>-<session slug>.md
```

### 5. plan_exit 工具与 plan → build 交接

文件：`packages/opencode/src/tool/plan.ts`

- `PlanExitTool` 是唯一的 plan 专用工具（id `plan_exit`），参数为空。
- 执行时用 question 服务问用户："Plan 已完成，是否切换到 build agent 开始实现？"
- 用户选 No → 抛 `Question.RejectedError`，继续留在 plan agent。
- 用户选 Yes → `session.updateMessage` 插入一条 `agent: "build"`、内容为 "The plan at ... has been approved, you can now edit files. Execute the plan" 的合成 user 消息，然后返回。

注册条件：`packages/opencode/src/tool/registry.ts`（约 243 行）

```ts
...(flags.experimentalPlanMode && flags.client === "cli" ? [tool.plan] : []),
```

即 plan_exit 只在实验模式 + CLI 客户端下暴露给模型。

### 6. agent 切换与入口

- TUI：Tab / Shift+Tab 触发 `agent.cycle` / `agent.cycle.reverse` 命令。定义在 `packages/tui/src/config/keybind.ts`（约 130-131 行）、`packages/tui/src/app.tsx`（约 696/730 行）。
- TUI 监听工具完成事件：`plan_exit` 完成后 `local.agent.set("build")`，`plan_enter` 完成后 `local.agent.set("plan")`（`packages/tui/src/routes/session/index.tsx`，约 328-333 行）。
- CLI：`opencode run --agent plan "..."`；agent 解析在 `packages/opencode/src/cli/cmd/run.ts`（`localAgent()`，约 594-613 行）。
- 非交互模式下 `run.ts` 会给会话加规则：`question`、`plan_enter`、`plan_exit` 全部 deny（约 437-446 行），所以无头模式不能走提问/plan 交接流程。

## 注意点

- 本快照中 `plan_enter` 只作为 permission action 存在（agent.ts / core plugin / run.ts），源码里没有 `plan_enter` 工具的 `Tool.define` 定义；TUI 仍保留对 `plan_enter` tool part 的监听，疑似为插件/旧版本兼容。
- plan agent 的权限默认没有显式 deny `bash`（默认 `"*": "allow"`），对 bash 的限制主要来自 plan-mode 系统提示词；文件修改则同时有权限硬拦截。
- 该仓库没有 `.git` 目录，无法标注 commit；以上均基于当前 `/home/lsy/opencode-dev` 快照（文件时间 2026-08-04）。
