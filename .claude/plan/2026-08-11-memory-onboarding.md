# Hermes 式文件记忆 + 首次引导方案

## 目标

让 embedded-agent 具备与 Hermes 一致的跨会话身份/偏好记忆：

- `USER.md` / `MEMORY.md` 文件型策展记忆，删库不丢；
- 首次启动对话式引导（profile build）；
- `memory` 工具可写文件型记忆；
- 保留 `memory.db` 承担语义检索/事实图谱，不互相替代。

## 现状

- `SystemPrompt` 已有 `# User Profile` 渲染槽位，但 `user_profile` 从未被填充。
- `MemoryTool` 只能读写 HolographicMemory（DB）。
- 无首次引导机制，无 USER.md/MEMORY.md。
- `config.toml` 无 `[memory.files]`、`[onboarding]`。

## 改动清单

### 1. 配置

`src/config/Config.h`：

```cpp
struct MemoryFilesConfig {
    bool enable = true;
    std::string dir;              // 空 = ~/.embedded-agent/memories
    int memory_char_limit = 2200;
    int user_char_limit = 1375;
};

struct MemoryConfig {
    // ...现有字段
    MemoryFilesConfig files;      // 对应 [memory.files]
};

struct OnboardingConfig {
    bool profile_build = true;        // 是否允许首次引导
    bool profile_build_offered = false; // 一次性 latch
};
```

更新 `TomlConversion.h`、`configs/default.toml`。

### 2. 文件记忆存储

新增 `src/memory/CuratedMemoryStore.h/.cpp`（加入 `ea-memory`）：

- 路径：`<dir>/MEMORY.md`、`<dir>/USER.md`；
- 条目分隔符：`§`（与 Hermes 一致）；
- `load()` / `add(target, content)` / `remove(target, substring)` /
  `list(target)` / `format_for_system_prompt(target)`；
- 字符上限、原子写入（tmp + rename）、文件锁；
- 无文件时自动创建空文件。

### 3. SystemPrompt 接线

- `PromptContext` 增加 `std::string curated_memory`（渲染 `# Persistent Memory`）；
- `AgentLoop` 持有 `CuratedMemoryStore*`；
- `build_system_prompt_once()` 加载 `USER.md` → `user_profile`，
  `MEMORY.md` → `curated_memory`；
- `AppBuilder` 创建 store 并传给 `AgentLoop` / `MemoryTool`。

### 4. MemoryTool 扩展

- 构造函数增加 `CuratedMemoryStore*`；
- schema 增加 `target`（`user` / `memory`）与 `add` / `list` / `remove` 动作；
- `target=user` 写 USER.md，`target=memory` 写 MEMORY.md；
- 原有 `store/recall/forget` 继续走 memory.db。

### 5. 首次引导

新增 `src/app/onboarding.h/.cpp`：

- `should_offer_profile(AppConfig&)`：无会话 + USER.md 为空 + 未 offer 过；
- `profile_build_directive()`：Hermes 同款指令文本；
- `mark_profile_offered(AppConfig&)`：写回 config。

`TuiRunner`：

- 启动时计算 `onboarding_directive`；
- 传入 `AgentLoop::Config.onboarding_directive`；
- `AgentLoop::run()` 首次调用时把 directive 拼到第一条用户消息后，
  并立即 `mark_profile_offered`。

### 6. 测试

- `CuratedMemoryStore` 单测：解析/写入/删除/字符上限/原子性；
- `MemoryTool` 集成：target=user 写 USER.md；
- `SystemPrompt` 单测：USER.md 内容进入 `# User Profile`；
- `onboarding` 单测：首次判定 + latch + 指令注入；
- 配置读写测试。

## 验收标准

- 删除 conversations.db/memory.db 后，USER.md 中的名字/偏好仍然进入 system prompt；
- `memory` 工具可写 USER.md/MEMORY.md，且下次会话生效；
- 首次启动只 offer 一次，拒绝后不再打扰；
- 原有 memory.db 语义检索/事实图谱功能不受影响；
- 全部测试通过。
