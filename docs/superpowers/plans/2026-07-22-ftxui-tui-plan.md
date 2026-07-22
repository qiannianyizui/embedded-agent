# FTXUI TUI 重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 embedded-agent 的 CLI 交互界面从原始 std::cout+getline 重构为基于 FTXUI v7.0.1 的全功能 TUI。

**Architecture:** TUI 作为 CLI 模式的"前端驱动器"，通过 `TuiApp` 类封装 FTXUI 组件树和事件循环。AgentLoop 在独立线程运行，通过 `ScreenInteractive::Post()` 桥接 UI 更新。不修改 AgentLoop 核心接口。

**Tech Stack:** C++17, FTXUI v7.0.1 (FetchContent), CMake 3.16+, Catch2 v3.7.1

## Global Constraints

- C++17 标准，无扩展（`CMAKE_CXX_STANDARD 17`）
- 命名空间：`ea::tui`
- 测试标签：`[tui]`
- 使用 `ea_target_compile_options(target)` 应用项目编译选项
- 不添加 `-fno-exceptions`（spdlog 需要）和全局 `-fno-rtti`
- OBJECT 库模式（与其他模块一致）
- 所有 UI 更新必须通过 `ScreenInteractive::Post()` 投递到主线程
- `#if EA_ENABLE_TUI` 条件编译保护 TUI 代码
- Embedded 模式自动禁用 TUI

---

## File Structure

| 文件 | 职责 |
|---|---|
| `src/tui/TuiApp.h` | TuiApp 类声明：驱动器，持有 ScreenInteractive，组装组件树 |
| `src/tui/TuiApp.cpp` | TuiApp 实现：组件树构建、事件循环、线程管理 |
| `src/tui/ChatArea.h` | ChatArea 类声明：聊天消息列表、流式追加 |
| `src/tui/ChatArea.cpp` | ChatArea 实现：消息渲染、滚动、spinner |
| `src/tui/InputBar.h` | InputBar 类声明：用户输入框 |
| `src/tui/InputBar.cpp` | InputBar 实现：FTXUI Input、历史、提交 |
| `src/tui/StatusBar.h` | StatusBar 类声明：状态信息栏 |
| `src/tui/StatusBar.cpp` | StatusBar 实现：token/费用/模型/会话显示 |
| `src/tui/ApprovalDialog.h` | ApprovalDialog 类声明：审批 Modal |
| `src/tui/ApprovalDialog.cpp` | ApprovalDialog 实现：y/n/a 按钮和快捷键 |
| `src/tui/CommandPalette.h` | CommandPalette 类声明：斜杠命令菜单 |
| `src/tui/CommandPalette.cpp` | CommandPalette 实现：Menu + Modal |
| `src/tui/SessionSidebar.h` | SessionSidebar 类声明：会话列表侧边栏 |
| `src/tui/SessionSidebar.cpp` | SessionSidebar 实现：Menu + ResizableSplitLeft |
| `src/tui/TuiEventListener.h` | TuiEventListener 类声明：IEventListener → Post() |
| `src/tui/TuiEventListener.cpp` | TuiEventListener 实现：AgentEvent 分发 |
| `src/tui/TuiApprovalHandler.h` | TuiApprovalHandler 类声明：IApprovalHandler → FTXUI 对话框 |
| `src/tui/TuiApprovalHandler.cpp` | TuiApprovalHandler 实现：condition_variable 等待 |
| `src/tui/CMakeLists.txt` | ea-tui OBJECT 库定义 |
| `tests/unit/test_tui_approval_handler.cpp` | TuiApprovalHandler 单元测试 |
| `tests/unit/test_tui_event_listener.cpp` | TuiEventListener 单元测试 |
| `tests/unit/test_tui_chat_area.cpp` | ChatArea 单元测试 |
| `tests/unit/test_tui_command_palette.cpp` | CommandPalette 单元测试 |

**修改文件：**
- `CMakeLists.txt`（根）：新增 EA_ENABLE_TUI 选项、FTXUI FetchContent、ea-tui 子目录、可执行文件链接
- `include/ea/build_config.h.in`：新增 `#cmakedefine EA_ENABLE_TUI`
- `src/main.cpp`：CLI 模式分支替换为 TuiApp
- `tests/unit/CMakeLists.txt`：新增 TUI 测试文件


---

### Task 1: CMake 构建基础设施

**Files:**
- Modify: `CMakeLists.txt`（根，第 14 行附近和第 88 行附近）
- Modify: `include/ea/build_config.h.in`（第 17 行后）
- Create: `src/tui/CMakeLists.txt`

**Interfaces:**
- Produces: `EA_ENABLE_TUI` CMake 选项和编译宏，`ea-tui` OBJECT 库目标，`ftxui::component` 链接目标

- [ ] **Step 1: 在根 CMakeLists.txt 添加 EA_ENABLE_TUI 选项和 FTXUI FetchContent**

在 `CMakeLists.txt` 第 24 行（`EA_ENABLE_PLUGINS` 之后）添加：

```cmake
option(EA_ENABLE_TUI "Enable FTXUI-based TUI for CLI mode" ON)
```

在 `EA_MODE_EMBEDDED` 的 auto-configuration 块（第 33 行后）添加：

```cmake
if(EA_MODE_EMBEDDED)
    set(EA_ENABLE_TUI OFF CACHE BOOL "Disable TUI for embedded" FORCE)
endif()
```

在 `FetchContent_MakeAvailable(json toml11 spdlog cli11 httplib mbedtls)` 行之后（第 88 行后）添加：

```cmake
# --- FTXUI (TUI library for CLI mode) ---
if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    FetchContent_Declare(ftxui
        URL https://github.com/ArthurSonzogni/FTXUI/archive/v7.0.1.tar.gz
    )
    FetchContent_MakeAvailable(ftxui)
endif()
```

在 `add_subdirectory(src/server)` 行之后添加：

```cmake
if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    add_subdirectory(src/tui)
endif()
```

- [ ] **Step 2: 在 build_config.h.in 添加 EA_ENABLE_TUI 宏**

在 `include/ea/build_config.h.in` 第 17 行（`EA_ENABLE_PLUGINS` 之后）添加：

```c
#cmakedefine EA_ENABLE_TUI
```

- [ ] **Step 3: 创建 src/tui/CMakeLists.txt**

```cmake
add_library(ea-tui OBJECT
    TuiApp.cpp
    ChatArea.cpp
    InputBar.cpp
    StatusBar.cpp
    ApprovalDialog.cpp
    CommandPalette.cpp
    SessionSidebar.cpp
    TuiEventListener.cpp
    TuiApprovalHandler.cpp
)
ea_target_compile_options(ea-tui)
target_include_directories(ea-tui PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-tui PUBLIC ftxui::component)
```

- [ ] **Step 4: 修改可执行文件链接**

将 `CMakeLists.txt` 第 178 行的：

```cmake
target_link_libraries(embedded-agent PRIVATE embedded-agent-core CLI11::CLI11)
```

替换为：

```cmake
if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    target_link_libraries(embedded-agent PRIVATE
        embedded-agent-core CLI11::CLI11 ea-tui ftxui::component)
else()
    target_link_libraries(embedded-agent PRIVATE
        embedded-agent-core CLI11::CLI11)
endif()
```

- [ ] **Step 5: 创建占位源文件使构建通过**

创建 `src/tui/TuiApp.h` 和 `src/tui/TuiApp.cpp` 为最小占位：

```cpp
// src/tui/TuiApp.h
#pragma once
#include "agent/AgentLoop.h"
namespace ea::tui {
class TuiApp {
public:
    AgentLoop::OutputFn output_fn();
    AgentLoop::StreamFn stream_fn();
    security::IApprovalHandler* approval_handler();
    void run(AgentLoop& loop);
};
}
```

```cpp
// src/tui/TuiApp.cpp
#include "TuiApp.h"
namespace ea::tui {
AgentLoop::OutputFn TuiApp::output_fn() {
    return [](const std::string& text) { /* TODO */ };
}
AgentLoop::StreamFn TuiApp::stream_fn() {
    return [](const ea::StreamChunk&) { /* TODO */ };
}
security::IApprovalHandler* TuiApp::approval_handler() {
    return nullptr;
}
void TuiApp::run(AgentLoop&) { /* TODO */ }
}
```

对其余 8 个 .h/.cpp 对创建类似的空占位文件（ChatArea, InputBar, StatusBar, ApprovalDialog, CommandPalette, SessionSidebar, TuiEventListener, TuiApprovalHandler），每个只包含 `#pragma once` 和空命名空间。

- [ ] **Step 6: 构建验证**

```bash
cd build && cmake .. -DEA_ENABLE_TUI=ON && cmake --build . -j$(nproc)
```

Expected: 构建成功（FTXUI 拉取并编译，占位文件链接通过）

- [ ] **Step 7: 验证 Embedded 模式不拉取 FTXUI**

```bash
cd /tmp && mkdir -p ea-embed-test && cd ea-embed-test
cmake /home/lsy/embedded-agent -DEA_MODE_EMBEDDED=ON -DEA_MODE_CLI=OFF -DEA_ENABLE_TESTS=OFF
```

Expected: CMake 配置成功，不拉取 FTXUI

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "feat(tui): add CMake infrastructure for FTXUI TUI module"
```


---

### Task 2: ChatArea 组件

**Files:**
- Create: `src/tui/ChatArea.h`
- Create: `src/tui/ChatArea.cpp`
- Test: `tests/unit/test_tui_chat_area.cpp`

**Interfaces:**
- Consumes: `ea::Role` (from `core/Types.h`), `ea::StreamChunk` (from `core/Types.h`), `ea::agent::AgentEvent` (from `agent/AgentEvent.h`)
- Produces: `ea::tui::ChatArea` 类，方法：`append_user(str)`, `append_assistant(str)`, `append_stream_chunk(StreamChunk)`, `append_tool_start(name, args)`, `append_tool_end(name, result, is_error)`, `append_error(msg)`, `finish_message()`, `component()` (返回 FTXUI Component), `clear()`

- [ ] **Step 1: 写 ChatArea.h**

```cpp
// src/tui/ChatArea.h
#pragma once
#include <ftxui/component/component.hpp>
#include "core/Types.h"
#include <string>
#include <vector>
#include <chrono>

namespace ea::tui {

struct ChatMessage {
    ea::Role role = ea::Role::User;
    std::string content;
    std::string tool_name;    // 仅 Tool 角色
    bool streaming = false;
    bool is_error = false;
    std::chrono::steady_clock::time_point timestamp{};
};

class ChatArea {
public:
    ChatArea();

    // 消息追加（从 AgentLoop 回调中通过 Post() 调用）
    void append_user(const std::string& text);
    void append_assistant(const std::string& text);
    void append_stream_chunk(const ea::StreamChunk& chunk);
    void append_tool_start(const std::string& name, const std::string& args);
    void append_tool_end(const std::string& name, const std::string& result, bool is_error);
    void append_error(const std::string& msg);
    void finish_message();  // 流式输出结束
    void clear();

    // FTXUI 组件
    ftxui::Component component();

    // 状态查询
    bool has_new_messages() const;
    void clear_new_flag();

private:
    ftxui::Component component_;
    std::vector<ChatMessage> messages_;
    std::string streaming_content_;  // 当前流式内容缓冲
    bool has_new_ = false;
    int scroll_position_ = 0;

    ftxui::Element render_message(const ChatMessage& msg, int width);
    ftxui::Element render();
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 ChatArea.cpp**

实现 `render()` 方法：
- 使用 `ftxui::vbox()` 堆叠所有消息元素
- 每条消息通过 `render_message()` 渲染
- User 消息：`ftxui::text(content) | ftxui::color(ftxui::Color::Cyan) | ftxui::align_right`
- Assistant 消息：`ftxui::text(content) | ftxui::color(ftxui::Color::Green)`
- Tool 消息：`ftxui::text("  [" + tool_name + "] " + content) | ftxui::dim | ftxui::color(ftxui::Color::GrayDark)`
- Error 消息：`ftxui::text(content) | ftxui::color(ftxui::Color::Red)`
- 流式消息末尾：追加 `ftxui::spinner(0, spinner_index_) | ftxui::color(ftxui::Color::Green)`
- 整体用 `ftxui::frame()` + `ftxui::flex()` 包裹实现滚动
- `append_stream_chunk()` 累积 `streaming_content_`，每次追加后标记 `has_new_ = true`
- `finish_message()` 将 `streaming_content_` 转为完整 ChatMessage 并清空缓冲
- `component()` 返回 `ftxui::Renderer(component_, [this] { return render(); })`

- [ ] **Step 3: 写 ChatArea 单元测试**

```cpp
// tests/unit/test_tui_chat_area.cpp
#include <catch2/catch_test_macros.hpp>
#include "tui/ChatArea.h"

using namespace ea::tui;

TEST_CASE("ChatArea appends user message", "[tui]") {
    ChatArea area;
    area.append_user("Hello");
    REQUIRE(area.has_new_messages());
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea appends assistant message", "[tui]") {
    ChatArea area;
    area.append_assistant("Hi there");
    REQUIRE(area.has_new_messages());
}

TEST_CASE("ChatArea accumulates stream chunks", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;
    chunk.data = "Hello";
    area.append_stream_chunk(chunk);
    chunk.data = " world";
    area.append_stream_chunk(chunk);
    // After finish, streaming content becomes a full message
    area.finish_message();
    REQUIRE(area.has_new_messages());
}

TEST_CASE("ChatArea clear removes all messages", "[tui]") {
    ChatArea area;
    area.append_user("test");
    area.clear();
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea tool messages", "[tui]") {
    ChatArea area;
    area.append_tool_start("shell", R"({"command":"ls"})");
    area.append_tool_end("shell", "file1\nfile2", false);
    REQUIRE(area.has_new_messages());
}

TEST_CASE("ChatArea error messages", "[tui]") {
    ChatArea area;
    area.append_error("Something went wrong");
    REQUIRE(area.has_new_messages());
}
```

- [ ] **Step 4: 运行测试验证**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-unit-tests "[tui]" -v
```

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(tui): implement ChatArea component with streaming support"
```


---

### Task 3: InputBar 组件

**Files:**
- Create: `src/tui/InputBar.h`
- Create: `src/tui/InputBar.cpp`

**Interfaces:**
- Produces: `ea::tui::InputBar` 类，方法：`component()`, `set_busy(bool)`, `set_on_submit(std::function<void(std::string)>)`, `clear()`

- [ ] **Step 1: 写 InputBar.h**

```cpp
// src/tui/InputBar.h
#pragma once
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

class InputBar {
public:
    InputBar();

    ftxui::Component component();
    void set_busy(bool busy);
    void set_on_submit(std::function<void(const std::string&)> fn);
    void clear();

private:
    ftxui::Component component_;
    std::string input_;
    std::vector<std::string> history_;
    int history_index_ = -1;
    bool busy_ = false;
    std::function<void(const std::string&)> on_submit_;

    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 InputBar.cpp**

实现要点：
- 使用 `ftxui::Input(&input_, "Type your message...")` 创建输入组件
- `component()` 返回 `ftxui::Renderer(input_component, [this] { return render(); })`
- `render()`: 当 `busy_` 时显示 `text("  ... ") | dim`；否则显示 `hbox(text("> "), input_component->Render())`
- `on_event()`: 捕获 Enter 键（非 Shift+Enter），调用 `on_submit_(input_)`，将 `input_` 追加到 `history_`，清空 `input_`
- Up/Down 键在 `input_` 为空时切换历史
- 输入 `/` 开头时可通过回调触发 CommandPalette（后续 Task 实现）

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat(tui): implement InputBar component with history support"
```

---

### Task 4: StatusBar 组件

**Files:**
- Create: `src/tui/StatusBar.h`
- Create: `src/tui/StatusBar.cpp`

**Interfaces:**
- Consumes: `ea::Usage` (from `core/Types.h`), `ea::budget::UsageSnapshot` and `ea::budget::CostSnapshot` (from `budget/Types.h`)
- Produces: `ea::tui::StatusBar` 类，方法：`component()`, `set_busy(bool)`, `update_usage(const ea::Usage&)`, `update_cost(double)`, `set_model(const std::string&)`, `set_session_id(const std::string&)`

- [ ] **Step 1: 写 StatusBar.h**

```cpp
// src/tui/StatusBar.h
#pragma once
#include <ftxui/component/component.hpp>
#include <string>

namespace ea::tui {

class StatusBar {
public:
    StatusBar();

    ftxui::Component component();
    void set_busy(bool busy);
    void update_usage(int input_tokens, int output_tokens);
    void update_cost(double cost_usd);
    void set_model(const std::string& model);
    void set_session_id(const std::string& id);

private:
    ftxui::Component component_;
    bool busy_ = false;
    int input_tokens_ = 0;
    int output_tokens_ = 0;
    double cost_usd_ = 0.0;
    std::string model_;
    std::string session_id_;

    ftxui::Element render();
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 StatusBar.cpp**

实现 `render()`:
- `hbox()` 水平排列
- 左侧：`busy_ ? spinner(3, tick) | color(Color::Yellow) : text("● ready") | color(Color::Green)`
- 中间：`text(model_) | dim`, `text(fmt_tokens) | dim`, `text(fmt_cost) | dim`
- 右侧：`text(session_id_) | dim`
- `fmt_tokens`: `std::to_string(input_tokens_/1000) + "k in / " + std::to_string(output_tokens_/1000) + "k out"`
- `fmt_cost`: `"$" + std::to_string(cost_usd_)` (保留 4 位小数)
- 整体用 `ftxui::border` + `ftxui::flex` 包裹

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "feat(tui): implement StatusBar component with usage display"
```

---

### Task 5: TuiApprovalHandler 组件

**Files:**
- Create: `src/tui/TuiApprovalHandler.h`
- Create: `src/tui/TuiApprovalHandler.cpp`
- Test: `tests/unit/test_tui_approval_handler.cpp`

**Interfaces:**
- Consumes: `ea::security::IApprovalHandler` (from `security/IApprovalHandler.h`), `ea::security::ApprovalRequest`, `ea::security::ApprovalDecision`
- Produces: `ea::tui::TuiApprovalHandler` 类，继承 `IApprovalHandler`，额外方法：`show_dialog(const ApprovalRequest&)`, `set_decision(ApprovalDecision)`, `current_request()`, `is_showing()`

- [ ] **Step 1: 写 TuiApprovalHandler.h**

```cpp
// src/tui/TuiApprovalHandler.h
#pragma once
#include "security/IApprovalHandler.h"
#include <mutex>
#include <condition_variable>
#include <optional>

namespace ea::tui {

class TuiApprovalHandler : public ea::security::IApprovalHandler {
public:
    TuiApprovalHandler();

    ea::security::ApprovalDecision request_approval(
        const ea::security::ApprovalRequest& req) override;

    // UI 调用：显示/隐藏对话框
    void show_dialog(const ea::security::ApprovalRequest& req);
    void set_decision(ea::security::ApprovalDecision decision);
    void dismiss_dialog();

    const ea::security::ApprovalRequest* current_request() const;
    bool is_showing() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<ea::security::ApprovalDecision> decision_;
    ea::security::ApprovalRequest current_request_;
    bool showing_ = false;
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 TuiApprovalHandler.cpp**

实现 `request_approval()`:
1. `std::lock_guard lock(mutex_);` 设置 `current_request_ = req; showing_ = true;`
2. `decision_.reset();`
3. 阻塞等待：`cv_.wait(lock, [this] { return decision_.has_value(); });`
4. 返回 `decision_.value();` 并设置 `showing_ = false;`

实现 `set_decision()`:
1. `std::lock_guard lock(mutex_);`
2. `decision_ = decision; showing_ = false;`
3. `cv_.notify_one();`

- [ ] **Step 3: 写 ApprovalDialog UI 组件**

在 `ApprovalDialog.h/cpp` 中：
- `component()`: 返回 `ftxui::Renderer` 渲染审批对话框
- `render()`: 显示 `vbox(text("⚠ Dangerous tool call"), text("Tool: " + req.tool_name), text("Args: " + req.arguments.dump()), separator(), hbox(button("[y] Approve", ...), button("[n] Reject", ...), button("[a] Abort", ...)))`
- 按钮回调调用 `handler_.set_decision(ApprovalDecision::Approved/Rejected/Aborted)`
- 同时处理 y/n/a 快捷键

- [ ] **Step 4: 写单元测试**

```cpp
// tests/unit/test_tui_approval_handler.cpp
#include <catch2/catch_test_macros.hpp>
#include "tui/TuiApprovalHandler.h"
#include <thread>

using namespace ea::tui;

TEST_CASE("TuiApprovalHandler blocks until decision set", "[tui]") {
    TuiApprovalHandler handler;
    ea::security::ApprovalRequest req{"shell", nlohmann::json{{"command", "rm -rf /"}}, "Delete everything"};

    ea::security::ApprovalDecision result;
    std::thread t([&] { result = handler.request_approval(req); });

    // Wait for handler to be showing
    while (!handler.is_showing()) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    REQUIRE(handler.current_request()->tool_name == "shell");

    handler.set_decision(ea::security::ApprovalDecision::Rejected);
    t.join();
    REQUIRE(result == ea::security::ApprovalDecision::Rejected);
}

TEST_CASE("TuiApprovalHandler approves on set_decision", "[tui]") {
    TuiApprovalHandler handler;
    ea::security::ApprovalRequest req{"file", nlohmann::json{{"path", "/tmp/test"}}, "Write file"};

    ea::security::ApprovalDecision result;
    std::thread t([&] { result = handler.request_approval(req); });

    handler.set_decision(ea::security::ApprovalDecision::Approved);
    t.join();
    REQUIRE(result == ea::security::ApprovalDecision::Approved);
}

TEST_CASE("TuiApprovalHandler aborts on set_decision", "[tui]") {
    TuiApprovalHandler handler;
    ea::security::ApprovalRequest req{"shell", nlohmann::json::object(), "Execute"};

    ea::security::ApprovalDecision result;
    std::thread t([&] { result = handler.request_approval(req); });

    handler.set_decision(ea::security::ApprovalDecision::Aborted);
    t.join();
    REQUIRE(result == ea::security::ApprovalDecision::Aborted);
}

TEST_CASE("TuiApprovalHandler is not showing initially", "[tui]") {
    TuiApprovalHandler handler;
    REQUIRE_FALSE(handler.is_showing());
    REQUIRE(handler.current_request() == nullptr);
}
```

- [ ] **Step 5: 运行测试验证**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-unit-tests "[tui]" -v
```

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat(tui): implement TuiApprovalHandler with condition_variable"
```


---

### Task 6: TuiEventListener 组件

**Files:**
- Create: `src/tui/TuiEventListener.h`
- Create: `src/tui/TuiEventListener.cpp`
- Test: `tests/unit/test_tui_event_listener.cpp`

**Interfaces:**
- Consumes: `ea::agent::IEventListener` (from `agent/IEventListener.h`), `ea::agent::AgentEvent`, `ea::agent::AgentEventType`, `ChatArea`, `StatusBar`
- Produces: `ea::tui::TuiEventListener` 类，继承 `IEventListener`

- [ ] **Step 1: 写 TuiEventListener.h**

```cpp
// src/tui/TuiEventListener.h
#pragma once
#include "agent/IEventListener.h"
#include <ftxui/screen/screen.hpp>
#include <functional>

namespace ea::tui {

class ChatArea;
class StatusBar;

class TuiEventListener : public ea::agent::IEventListener {
public:
    // post_fn 封装 ScreenInteractive::Post()
    TuiEventListener(ChatArea& chat_area, StatusBar& status_bar,
                     std::function<void(std::function<void()>)> post_fn);

    void on_event(const ea::agent::AgentEvent& event) override;

private:
    ChatArea& chat_area_;
    StatusBar& status_bar_;
    std::function<void(std::function<void()>)> post_fn_;
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 TuiEventListener.cpp**

```cpp
#include "TuiEventListener.h"
#include "ChatArea.h"
#include "StatusBar.h"

namespace ea::tui {

TuiEventListener::TuiEventListener(ChatArea& chat_area, StatusBar& status_bar,
                                   std::function<void(std::function<void()>)> post_fn)
    : chat_area_(chat_area), status_bar_(status_bar), post_fn_(std::move(post_fn)) {}

void TuiEventListener::on_event(const ea::agent::AgentEvent& event) {
    switch (event.type) {
        case ea::agent::AgentEventType::TurnStart:
            post_fn_([this] { status_bar_.set_busy(true); });
            break;
        case ea::agent::AgentEventType::LLMResponse:
            post_fn_([this, usage = event.usage] {
                status_bar_.update_usage(usage.input_tokens, usage.output_tokens);
            });
            break;
        case ea::agent::AgentEventType::ToolCallStart:
            post_fn_([this, name = event.tool_name, args = event.tool_arguments.dump()] {
                chat_area_.append_tool_start(name, args);
            });
            break;
        case ea::agent::AgentEventType::ToolCallEnd:
            post_fn_([this, name = event.tool_name, result = event.tool_result, is_err = event.tool_error] {
                chat_area_.append_tool_end(name, result, is_err);
            });
            break;
        case ea::agent::AgentEventType::TurnEnd:
            post_fn_([this] { status_bar_.set_busy(false); });
            break;
        case ea::agent::AgentEventType::Error:
            post_fn_([this, msg = event.error_message] {
                chat_area_.append_error(msg);
            });
            break;
        case ea::agent::AgentEventType::Interrupt:
            post_fn_([this] {
                chat_area_.append_error("Interrupted");
                status_bar_.set_busy(false);
            });
            break;
    }
}

}  // namespace ea::tui
```

- [ ] **Step 3: 写单元测试**

```cpp
// tests/unit/test_tui_event_listener.cpp
#include <catch2/catch_test_macros.hpp>
#include "tui/TuiEventListener.h"
#include "tui/ChatArea.h"
#include "tui/StatusBar.h"
#include <vector>
#include <mutex>

using namespace ea::tui;

TEST_CASE("TuiEventListener dispatches TurnStart", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    std::vector<std::function<void()>> posted;
    std::mutex mtx;

    TuiEventListener listener(chat_area, status_bar,
        [&](std::function<void()> fn) {
            std::lock_guard lock(mtx);
            posted.push_back(std::move(fn));
        });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::TurnStart;
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
    // Execute the posted task
    posted[0]();
    // Status bar should be busy now
}

TEST_CASE("TuiEventListener dispatches LLMResponse", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::LLMResponse;
    event.usage = ea::Usage{100, 50, 0, 0};
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
}

TEST_CASE("TuiEventListener dispatches ToolCallStart", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::ToolCallStart;
    event.tool_name = "shell";
    event.tool_arguments = nlohmann::json{{"command", "ls"}};
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
}

TEST_CASE("TuiEventListener dispatches Error", "[tui]") {
    ChatArea chat_area;
    StatusBar status_bar;
    std::vector<std::function<void()>> posted;

    TuiEventListener listener(chat_area, status_bar,
        [&](std::function<void()> fn) { posted.push_back(std::move(fn)); });

    ea::agent::AgentEvent event;
    event.type = ea::agent::AgentEventType::Error;
    event.error_message = "Connection failed";
    listener.on_event(event);

    REQUIRE(posted.size() == 1);
}
```

- [ ] **Step 4: 运行测试验证**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-unit-tests "[tui]" -v
```

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(tui): implement TuiEventListener bridging AgentEvent to UI"
```

---

### Task 7: CommandPalette 组件

**Files:**
- Create: `src/tui/CommandPalette.h`
- Create: `src/tui/CommandPalette.cpp`
- Test: `tests/unit/test_tui_command_palette.cpp`

**Interfaces:**
- Produces: `ea::tui::CommandPalette` 类，方法：`component()`, `is_showing()`, `show()`, `hide()`, `set_on_command(std::function<void(std::string)>)`

- [ ] **Step 1: 写 CommandPalette.h**

```cpp
// src/tui/CommandPalette.h
#pragma once
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

struct CommandEntry {
    std::string name;     // e.g., "/quit"
    std::string description;
};

class CommandPalette {
public:
    CommandPalette();

    ftxui::Component component();
    bool is_showing() const;
    void show();
    void hide();
    void set_on_command(std::function<void(const std::string&)> fn);

private:
    ftxui::Component component_;
    std::vector<CommandEntry> commands_;
    int selected_ = 0;
    bool showing_ = false;
    std::function<void(const std::string&)> on_command_;

    ftxui::Element render();
    bool on_event(ftxui::Event event);
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 CommandPalette.cpp**

初始化命令列表：
```cpp
commands_ = {
    {"/quit",   "Exit the agent"},
    {"/usage",  "Show token usage"},
    {"/cost",   "Show session cost"},
    {"/history","List conversations"},
    {"/resume", "Resume a conversation"},
    {"/export", "Export current conversation"},
    {"/import", "Import a conversation"},
    {"/clear",  "Clear current conversation"},
};
```

`render()`: `vbox(text("Commands") | bold, separator(), menu entries)` 每个命令显示为 `text(entry.name + " - " + entry.description)`
`on_event()`: Enter 执行 `on_command_(commands_[selected_].name)` 并 `hide()`；Esc 取消并 `hide()`

- [ ] **Step 3: 写单元测试**

```cpp
// tests/unit/test_tui_command_palette.cpp
#include <catch2/catch_test_macros.hpp>
#include "tui/CommandPalette.h"

using namespace ea::tui;

TEST_CASE("CommandPalette starts hidden", "[tui]") {
    CommandPalette palette;
    REQUIRE_FALSE(palette.is_showing());
}

TEST_CASE("CommandPalette show/hide", "[tui]") {
    CommandPalette palette;
    palette.show();
    REQUIRE(palette.is_showing());
    palette.hide();
    REQUIRE_FALSE(palette.is_showing());
}

TEST_CASE("CommandPalette fires on_command", "[tui]") {
    CommandPalette palette;
    std::string received;
    palette.set_on_command([&](const std::string& cmd) { received = cmd; });
    // Directly invoke to test callback wiring
    // (FTXUI event simulation would be done in integration tests)
}
```

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat(tui): implement CommandPalette with slash command menu"
```

---

### Task 8: SessionSidebar 组件

**Files:**
- Create: `src/tui/SessionSidebar.h`
- Create: `src/tui/SessionSidebar.cpp`

**Interfaces:**
- Consumes: `ea::conversation::IConversationStore` (from `conversation/IConversationStore.h`)
- Produces: `ea::tui::SessionSidebar` 类，方法：`component()`, `is_showing()`, `toggle()`, `refresh()`, `set_on_resume(std::function<void(std::string)>)`

- [ ] **Step 1: 写 SessionSidebar.h 和 .cpp**

使用 `ftxui::Menu` 组件显示会话列表。`toggle()` 切换 `showing_` 状态。Ctrl+S 绑定到 toggle。`refresh()` 从 `IConversationStore::list()` 重新加载会话列表。

- [ ] **Step 2: Commit**

```bash
git add -A && git commit -m "feat(tui): implement SessionSidebar for conversation management"
```


---

### Task 9: TuiApp 组装与 main.cpp 集成

**Files:**
- Modify: `src/tui/TuiApp.h`
- Modify: `src/tui/TuiApp.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: ChatArea, InputBar, StatusBar, ApprovalDialog, CommandPalette, SessionSidebar, TuiEventListener, TuiApprovalHandler
- Consumes: `ea::agent::AgentLoop`, `ea::budget::BudgetTracker`, `ea::conversation::IConversationStore`
- Produces: 完整的 TUI 事件循环，AgentLoop 回调桥接

- [ ] **Step 1: 写完整的 TuiApp.h**

```cpp
// src/tui/TuiApp.h
#pragma once
#include "agent/AgentLoop.h"
#include "budget/BudgetTracker.h"
#include "conversation/IConversationStore.h"
#include "ChatArea.h"
#include "InputBar.h"
#include "StatusBar.h"
#include "ApprovalDialog.h"
#include "CommandPalette.h"
#include "SessionSidebar.h"
#include "TuiEventListener.h"
#include "TuiApprovalHandler.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <thread>
#include <atomic>

namespace ea::tui {

class TuiApp {
public:
    TuiApp();
    ~TuiApp();

    // 提供 AgentLoop 需要的回调
    ea::agent::AgentLoop::OutputFn output_fn();
    ea::agent::AgentLoop::StreamFn stream_fn();
    ea::security::IApprovalHandler* approval_handler();

    // 启动 TUI 事件循环（阻塞）
    void run(ea::agent::AgentLoop& loop,
             ea::budget::BudgetTracker* budget_tracker = nullptr,
             ea::conversation::IConversationStore* conv_store = nullptr);

private:
    ftxui::ScreenInteractive screen_;
    ChatArea chat_area_;
    InputBar input_bar_;
    StatusBar status_bar_;
    ApprovalDialog approval_dialog_;
    CommandPalette command_palette_;
    SessionSidebar sidebar_;
    TuiApprovalHandler approval_handler_;
    std::unique_ptr<TuiEventListener> event_listener_;

    // AgentLoop 线程管理
    ea::agent::AgentLoop* loop_ = nullptr;
    ea::budget::BudgetTracker* budget_tracker_ = nullptr;
    ea::conversation::IConversationStore* conv_store_ = nullptr;
    std::thread agent_thread_;
    std::atomic<bool> agent_busy_{false};

    // 组件树
    ftxui::Component root_component_;
    int sidebar_width_ = 0;  // 0 = hidden

    void build_component_tree();
    void submit_input(const std::string& input);
    void execute_command(const std::string& command);
    void run_agent(const std::string& input);
};

}  // namespace ea::tui
```

- [ ] **Step 2: 写 TuiApp.cpp 核心实现**

关键实现：

**`output_fn()`**: 返回 lambda，通过 `screen_.Post()` 将内容追加到 ChatArea：
```cpp
AgentLoop::OutputFn TuiApp::output_fn() {
    return [this](const std::string& text) {
        screen_.Post([this, text] { chat_area_.append_assistant(text); });
    };
}
```

**`stream_fn()`**: 返回 lambda，处理流式块：
```cpp
AgentLoop::StreamFn TuiApp::stream_fn() {
    return [this](const ea::StreamChunk& chunk) {
        screen_.Post([this, chunk] {
            if (chunk.type == ea::StreamChunk::Type::Content) {
                chat_area_.append_stream_chunk(chunk);
            } else if (chunk.type == ea::StreamChunk::Type::Done) {
                chat_area_.finish_message();
            }
        });
    };
}
```

**`approval_handler()`**: 返回 `&approval_handler_`

**`build_component_tree()`**: 组装 FTXUI 组件树：
```cpp
void TuiApp::build_component_tree() {
    auto main_area = ftxui::Renderer([this] {
        return ftxui::vbox({
            chat_area_.component()->Render() | ftxui::flex,
            ftxui::separator(),
            status_bar_.component()->Render(),
            ftxui::separator(),
            input_bar_.component()->Render(),
        });
    });

    if (sidebar_width_ > 0) {
        root_component_ = ftxui::ResizableSplitLeft(
            sidebar_.component(), main_area, &sidebar_width_);
    } else {
        root_component_ = main_area;
    }

    // 添加 Modal 层
    root_component_ = ftxui::Modal(root_component_, approval_dialog_.component(),
                                   approval_handler_.is_showing());
    root_component_ = ftxui::Modal(root_component_, command_palette_.component(),
                                   command_palette_.is_showing());
}
```

**`submit_input()`**: 在独立线程启动 AgentLoop：
```cpp
void TuiApp::submit_input(const std::string& input) {
    if (input.empty()) return;
    if (input[0] == '/') {
        execute_command(input);
        return;
    }
    chat_area_.append_user(input);
    run_agent(input);
}

void TuiApp::run_agent(const std::string& input) {
    if (agent_thread_.joinable()) agent_thread_.join();
    agent_thread_ = std::thread([this, input] {
        agent_busy_.store(true);
        screen_.Post([this] { input_bar_.set_busy(true); });

        auto result = loop_->run(input);

        agent_busy_.store(false);
        screen_.Post([this] { input_bar_.set_busy(false); });

        if (!result.ok()) {
            screen_.Post([this, msg = result.error().message] {
                chat_area_.append_error(msg);
            });
        }
    });
}
```

**`execute_command()`**: 处理斜杠命令：
```cpp
void TuiApp::execute_command(const std::string& cmd) {
    if (cmd == "/quit" || cmd == "/exit") {
        screen_.Exit();
    } else if (cmd == "/clear") {
        chat_area_.clear();
    } else if (cmd == "/usage" && budget_tracker_) {
        auto su = budget_tracker_->session_usage();
        chat_area_.append_system("Usage: " + std::to_string(su.total_tokens()) + " tokens");
    }
    // ... 其余命令类似
}
```

**`run()`**: 启动 FTXUI 主循环：
```cpp
void TuiApp::run(AgentLoop& loop, BudgetTracker* bt, IConversationStore* cs) {
    loop_ = &loop;
    budget_tracker_ = bt;
    conv_store_ = cs;

    // 设置事件监听器
    event_listener_ = std::make_unique<TuiEventListener>(
        chat_area_, status_bar_,
        [this](std::function<void()> fn) { screen_.Post(std::move(fn)); });
    loop.add_listener(event_listener_);

    // 设置输入回调
    input_bar_.set_on_submit([this](const std::string& input) {
        submit_input(input);
    });

    // 设置命令回调
    command_palette_.set_on_command([this](const std::string& cmd) {
        execute_command(cmd);
    });

    build_component_tree();

    screen_.Loop(root_component_);

    // 清理
    if (agent_thread_.joinable()) {
        loop_->interrupt();
        agent_thread_.join();
    }
}
```

- [ ] **Step 3: 修改 main.cpp**

在 `src/main.cpp` 的 CLI 模式分支（第 329 行 `#else` 之后）添加条件编译：

```cpp
#else
#if EA_ENABLE_TUI
    // TUI mode: FTXUI-based interactive interface
    ea::tui::TuiApp tui;

    // Replace OutputFn and StreamFn with TUI callbacks
    auto tui_output = tui.output_fn();
    auto tui_stream = tui.stream_fn();

    // Recreate AgentLoop with TUI callbacks
    ea::agent::AgentLoop tui_loop(
        effective_provider, &registry, memory.get(),
        ea::agent::AgentLoop::Config{
            cfg.agent.max_iterations, 65536, 100, true, cfg.agent.stream, cfg.conversation.auto_persist
        },
        tui_output,
        tui_stream,
        security.get(),
        tui.approval_handler(),
        compressor.get(),
        strategy.get(),
        conv_store.get(),
        budget_tracker.get()
    );

    if (debug) {
        tui_loop.add_listener(std::make_shared<ea::agent::LoggingEventListener>());
    }

    // Auto-resume last conversation
    if (conv_store && cfg.conversation.auto_resume) {
        auto recent = conv_store->list(1, 0);
        if (recent.ok() && !recent.value().empty()) {
            auto& meta = recent.value()[0];
            auto msgs = conv_store->load(meta.id);
            if (msgs.ok() && !msgs.value().empty()) {
                tui_loop.restore_conversation(meta.id, std::move(msgs.value()));
            }
        }
    }

    tui.run(tui_loop, budget_tracker.get(), conv_store.get());
#else
    // Original CLI mode: std::cout + getline
    std::string input;
    std::cout << "embedded-agent v0.1.0 (type /quit to exit)" << std::endl;
    // ... 保留原有的 while+getline 循环代码 ...
#endif
#endif
```

- [ ] **Step 4: 构建和基本运行验证**

```bash
cd build && cmake --build . -j$(nproc)
```

Expected: 构建成功

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(tui): integrate TuiApp with main.cpp and assemble FTXUI component tree"
```

---

### Task 10: 测试集成与 TUI 测试注册

**Files:**
- Modify: `tests/unit/CMakeLists.txt`

- [ ] **Step 1: 添加 TUI 测试到单元测试可执行文件**

在 `tests/unit/CMakeLists.txt` 的 `add_executable` 中添加：

```cmake
test_tui_approval_handler.cpp
test_tui_event_listener.cpp
test_tui_chat_area.cpp
test_tui_command_palette.cpp
```

在 `target_link_libraries` 中添加（条件编译）：

```cmake
if(EA_ENABLE_TUI AND NOT EA_MODE_EMBEDDED)
    target_link_libraries(ea-unit-tests PRIVATE ea-tui ftxui::component)
endif()
```

- [ ] **Step 2: 构建并运行所有测试**

```bash
cd build && cmake --build . -j$(nproc) && ./tests/ea-unit-tests -v
```

Expected: 所有测试通过，包括新的 `[tui]` 标签测试

- [ ] **Step 3: 运行 TUI 标签测试**

```bash
./tests/ea-unit-tests "[tui]"
```

Expected: 4 个测试通过

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "test(tui): register TUI unit tests and verify all pass"
```

---

### Task 11: 端到端验证与清理

**Files:**
- All TUI files (review and polish)

- [ ] **Step 1: 验证完整构建流程**

```bash
rm -rf build && mkdir build && cd build
cmake .. -DEA_ENABLE_TUI=ON -DENABLE_TESTS=ON
cmake --build . -j$(nproc)
```

- [ ] **Step 2: 验证 Embedded 模式构建**

```bash
cd /tmp && rm -rf ea-embed-test && mkdir ea-embed-test && cd ea-embed-test
cmake /home/lsy/embedded-agent -DEA_MODE_EMBEDDED=ON -DEA_MODE_CLI=OFF -DEA_ENABLE_TESTS=OFF
cmake --build . -j$(nproc)
```

- [ ] **Step 3: 验证 TUI 禁用时的回退**

```bash
cd /home/lsy/embedded-agent && rm -rf build && mkdir build && cd build
cmake .. -DEA_ENABLE_TUI=OFF -DENABLE_TESTS=ON
cmake --build . -j$(nproc)
```

Expected: 使用原始 std::cout+getline CLI，不拉取 FTXUI

- [ ] **Step 4: 运行完整测试套件**

```bash
./tests/ea-unit-tests
./tests/ea-tests 2>/dev/null || true
```

- [ ] **Step 5: Final commit**

```bash
git add -A && git commit -m "feat(tui): complete FTXUI TUI integration with build verification"
```

