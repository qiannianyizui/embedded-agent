# FTXUI TUI 重构设计

**日期**: 2026-07-22
**版本**: v1.0
**状态**: 待审阅

## 概述

将 embedded-agent 的 CLI 交互界面从原始 `std::cout + std::getline` 重构为基于 FTXUI v7.0.1 的全功能 TUI（Terminal User Interface）。TUI 作为 CLI 模式的"前端驱动器"，不修改 AgentLoop 核心接口，通过回调桥接实现 UI 交互。

## 目标

- 完全替换现有 CLI 交互循环（`while+getline`）
- 提供聊天式布局：上方聊天区 + 底部输入框 + 状态栏 + 可折叠侧边栏
- 实现实时反馈：流式输出、工具调用显示、spinner、审批对话框
- 实现状态栏/仪表盘：token 用量、费用、模型名、会话信息
- 实现斜杠命令菜单：可视化的命令发现与执行
- 实现会话管理：侧边栏列表、切换、恢复
- 保持 Embedded/Server 模式不受影响
- 提供 `EA_ENABLE_TUI` CMake 选项，可回退到原始 CLI

## 架构

### 模块结构

```
src/tui/
  TuiApp.h/cpp              — 驱动器：组装组件树、启动 ScreenInteractive 循环
  ChatArea.h/cpp             — 聊天区组件：消息列表、流式追加、滚动
  InputBar.h/cpp             — 输入框组件：多行输入、历史、斜杠命令
  StatusBar.h/cpp            — 状态栏组件：token/费用/模型/会话
  ApprovalDialog.h/cpp       — 审批对话框组件：Modal 覆盖
  CommandPalette.h/cpp       — 命令面板组件：斜杠命令列表
  SessionSidebar.h/cpp       — 会话侧边栏组件
  TuiEventListener.h/cpp     — IEventListener 实现：AgentEvent → Post() 更新 UI
  TuiApprovalHandler.h/cpp   — IApprovalHandler 实现：FTXUI 对话框替代 stdin
  CMakeLists.txt             — ea-tui OBJECT 库
```

### 组件树布局

```
ScreenInteractive::Fullscreen()
└── Renderer(root)
    ├── ResizableSplitLeft(sidebar, main, sidebar_width)
    │   ├── SessionSidebar
    │   └── vbox(
    │       ├── frame(flex, ChatArea)
    │       ├── hbox(StatusBar elements)
    │       └── InputBar
    │   )
    └── Modal(ApprovalDialog, show_approval)
    └── Modal(CommandPalette, show_commands)
```

### main.cpp 变化

初始化代码（步骤 1-8）完全不变，仅替换 CLI 模式分支：

```cpp
// 之前
#else
std::string input;
while (true) { std::cout << "> "; std::getline(std::cin, input); ... }
#endif

// 之后
#else
#if EA_ENABLE_TUI
    ea::tui::TuiApp tui(loop, budget_tracker.get(), conv_store.get());
    tui.run();
#else
    // 原始 std::cout + getline 回退
    while (true) { ... }
#endif
#endif
```

## 线程模型

```
主线程（FTXUI 事件循环）              AgentLoop 线程
  ├── 读取终端事件                     ├── loop.run(input)
  ├── 执行 Post() 任务队列             ├── StreamFn → Post(追加内容)
  ├── 渲染组件树                       ├── OutputFn → Post(完成消息)
  └── 输出差异到终端                   ├── IEventListener → Post(状态更新)
                                       └── IApprovalHandler → Post(审批请求)
```

- AgentLoop 在独立 `std::thread` 中运行，不阻塞 FTXUI 渲染
- 所有 UI 更新通过 `ScreenInteractive::Post()` 投递到主线程
- `TuiApprovalHandler` 使用 `condition_variable` 阻塞等待用户响应

## 组件详细设计

### ChatArea

**状态**：
```cpp
struct ChatMessage {
    ea::Role role;
    std::string content;
    std::string tool_name;  // 仅 Tool 角色
    bool streaming;
    std::chrono::steady_clock::time_point timestamp;
};
std::vector<ChatMessage> messages_;
```

**渲染**：
- `frame()` 可滚动视口
- User 消息右对齐蓝色，Assistant 消息左对齐绿色
- 流式消息末尾显示 spinner
- 工具调用显示为缩进灰色块
- 新消息自动滚动到底部（`focus()` 最后元素）
- 滚动时暂停自动跟随，新消息到达显示"↓ 新消息"提示

**滚动条**：自行实现 1 列宽滚动条指示器，使用 `gauge()` 渲染位置比例。

### InputBar

**状态**：
```cpp
std::string input_content_;
std::vector<std::string> history_;
int history_index_ = -1;
bool multiline_ = false;
```

**渲染**：
- 底部固定区域，FTXUI `Input` 组件
- 提示符 `> `，流式输出时禁用输入并显示 `...`
- 多行模式：Shift+Enter 换行，Enter 提交
- 输入 `/` 时弹出 CommandPalette

**交互**：
- Enter → `Post()` 投递到 AgentLoop 线程
- Up/Down（光标在行首/行尾）切换历史
- Tab 触发补全
- Ctrl+C 中断 AgentLoop

### StatusBar

**渲染**：
```
[● thinking] [deepseek-chat] [1.2k in / 800 out | $0.0123] [session: abc123] [3m 42s]
```

- 左侧：忙碌指示器（spinner + "thinking"/"ready"）
- 中间：模型名、token 用量、费用
- 右侧：会话 ID、运行时长
- 宽度不足时渐进隐藏低优先级信息

**更新**：`TuiEventListener` 接收 `AgentEvent::LLMResponse` 更新 token/费用。

### ApprovalDialog

- FTXUI `Modal` 覆盖主界面
- 显示工具名、参数、描述
- 三个按钮：`[y] Approve` `[n] Reject` `[a] Abort`
- 快捷键 y/n/a 直接响应
- 响应通过 `condition_variable` 传递给 `TuiApprovalHandler`

### CommandPalette

**命令列表**：

| 命令 | 描述 |
|---|---|
| /quit | 退出 |
| /usage | 显示 token 用量 |
| /cost | 显示费用 |
| /history | 历史会话 |
| /resume | 恢复会话 |
| /export | 导出对话 |
| /import | 导入对话 |
| /clear | 清空当前对话 |

**渲染**：`Modal` + `Menu` 组件，Up/Down 选择，Enter 执行，Esc 取消。

### SessionSidebar

- `ResizableSplitLeft` 可折叠侧边栏
- `Menu` 组件列出历史会话（标题 + 消息数 + 时间）
- 当前会话高亮
- Ctrl+S 切换显示/隐藏

### TuiEventListener

```cpp
class TuiEventListener : public ea::agent::IEventListener {
    void on_event(const ea::agent::AgentEvent& event) override {
        screen_.Post([this, event] {
            switch (event.type) {
                case AgentEventType::TurnStart:   status_bar_.set_busy(true); break;
                case AgentEventType::LLMResponse:  status_bar_.update_usage(event.usage); break;
                case AgentEventType::ToolCallStart: chat_area_.append_tool_start(event); break;
                case AgentEventType::ToolCallEnd:   chat_area_.append_tool_end(event); break;
                case AgentEventType::TurnEnd:       status_bar_.set_busy(false); break;
                case AgentEventType::Error:         chat_area_.append_error(event.error_message); break;
            }
        });
    }
};
```

### TuiApprovalHandler

```cpp
class TuiApprovalHandler : public ea::security::IApprovalHandler {
    ApprovalDecision request_approval(const ApprovalRequest& req) override {
        // 1. Post() 投递审批请求到 UI
        screen_.Post([this, req] { show_approval_dialog(req); });
        // 2. 阻塞等待用户响应
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return decision_.has_value(); });
        // 3. 返回决策
        return decision_.value();
    }
    
    void set_decision(ApprovalDecision d) {
        std::lock_guard lock(mutex_);
        decision_ = d;
        cv_.notify_one();
    }
};
```

## CMake 集成

### 顶层 CMakeLists.txt 新增

```cmake
option(EA_ENABLE_TUI "Enable FTXUI-based TUI for CLI mode" ON)

if(EA_MODE_EMBEDDED)
    set(EA_ENABLE_TUI OFF CACHE BOOL "Disable TUI for embedded" FORCE)
endif()

if(EA_ENABLE_TUI)
    FetchContent_Declare(ftxui
        URL https://github.com/ArthurSonzogni/FTXUI/archive/v7.0.1.tar.gz
    )
    FetchContent_MakeAvailable(ftxui)
    add_subdirectory(src/tui)
endif()
```

### src/tui/CMakeLists.txt

```cmake
add_library(ea-tui OBJECT
    TuiApp.cpp ChatArea.cpp InputBar.cpp StatusBar.cpp
    ApprovalDialog.cpp CommandPalette.cpp SessionSidebar.cpp
    TuiEventListener.cpp TuiApprovalHandler.cpp
)
ea_target_compile_options(ea-tui)
target_include_directories(ea-tui PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-tui PUBLIC ftxui::component)
```

### 可执行文件链接

```cmake
if(EA_ENABLE_TUI)
    target_link_libraries(embedded-agent PRIVATE
        embedded-agent-core CLI11::CLI11 ea-tui ftxui::component)
else()
    target_link_libraries(embedded-agent PRIVATE
        embedded-agent-core CLI11::CLI11)
endif()
```

### build_config.h.in 新增

```c
#cmakedefine EA_ENABLE_TUI
```

## 测试策略

### 单元测试

| 组件 | 测试内容 | 标签 |
|---|---|---|
| TuiEventListener | AgentEvent → Post() 调用验证 | `[tui]` |
| TuiApprovalHandler | 审批请求/响应流程 | `[tui]` |
| ChatArea | 消息追加、流式内容 | `[tui]` |
| InputBar | 输入提交、历史切换 | `[tui]` |
| CommandPalette | 命令列表、选择执行 | `[tui]` |

### 集成测试

- 启动 TUI + MockProvider，验证完整交互流程
- 审批对话框弹出/响应
- 流式输出显示
- 中断（Ctrl+C）处理

### 测试方法

FTXUI 组件通过 `ComponentBase::Render()` 直接获取 DOM 元素，无需真实终端。使用 `Event::Special()` 模拟用户输入。

## 风险与缓解

| 风险 | 缓解措施 |
|---|---|
| FTXUI `frame()` 无显式滚动条 | 自行实现 1 列宽滚动条指示器（`gauge()` 渲染） |
| AgentLoop 线程与 FTXUI 主线程竞态 | 所有 UI 更新必须通过 `Post()`，状态变量用 `std::atomic` 或 mutex 保护 |
| `TuiApprovalHandler` 阻塞 AgentLoop 线程 | `condition_variable` 等待 + 超时机制防止死锁 |
| FTXUI 编译时间增加（~100 源文件） | FetchContent 预编译；Embedded 模式不拉取 |
| 终端兼容性（老旧终端不支持 TrueColor） | FTXUI 自动检测终端能力并降级；`NO_COLOR` 环境变量支持 |
| v7.0.0 破坏性重命名 | 使用 v7.0.1 兼容别名，代码中统一使用新名称 |

## 参考架构

hermes-agent 的 TUI 使用 React + Ink（TypeScript），其架构模式提供了以下参考：

| hermes 模式 | 本项目 FTXUI 对应 |
|---|---|
| React 组件树 | FTXUI 组件树（Container + Renderer） |
| Ink `<Box>` 布局 | FTXUI `vbox()`/`hbox()` + `flex` |
| `useState` 状态管理 | FTXUI `Ref<T>` + `Post()` 更新 |
| Gateway WebSocket 通信 | AgentLoop 回调 + `Post()` |
| Overlay 系统 | FTXUI `Modal` + `Container::Stacked` |
| Composer（输入框+历史+补全） | FTXUI `Input` + 自定义补全 |
| Transcript（聊天历史） | FTXUI `frame()` + `focus()` 滚动 |
| StatusBar | FTXUI 底部固定 `hbox()` |

关键区别：hermes 是双进程架构（Node.js TUI + Python Gateway），本项目是单进程架构，FTXUI 的 `Post()` 机制更适合直接集成。
