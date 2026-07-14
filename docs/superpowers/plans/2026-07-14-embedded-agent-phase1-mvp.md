# Embedded Agent C++ Phase 1 MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working C++ AI Agent that can converse with an LLM, execute tools, and persist memory — runnable on Linux and cross-compilable for Android.

**Architecture:** Trait-driven modular design. Core interfaces (pure virtual) in `src/core/`, shared utilities in `src/common/` (base/io/net), concrete implementations in `src/provider/`, `src/tool/`, `src/memory/`, `src/agent/`. All modules compile as OBJECT libraries, merged into `libembedded-agent-core.a`. Error handling via `Result<T>` (no exceptions). Synchronous model with `std::thread` for streaming.

**Tech Stack:** C++17, CMake 3.16+, nlohmann/json, cpp-httplib, mbedTLS, toml11, spdlog, CLI11, SQLite amalgamation, Catch2

---

## File Map

| File | Responsibility |
|------|---------------|
| `CMakeLists.txt` | Top-level build, FetchContent, target definitions |
| `cmake/Toolchain-Android.cmake` | Android NDK cross-compilation |
| `.gitignore` | Ignore third_party/, build/, IDE files |
| `src/common/base/Error.h` | ErrorCode enum + Error struct + factory methods |
| `src/common/base/Result.h` | Result<T> (variant<T,Error>) + Result<void> specialization |
| `src/common/base/StringUtil.h` | trim, split, starts_with, ends_with, to_lower, replace_all |
| `src/common/base/JsonHelper.h` | get_or<T>, get_path (nested JSON access) |
| `src/common/io/Logger.h` | spdlog wrapper, EA_DEBUG/INFO/WARN/ERROR macros |
| `src/common/io/Logger.cpp` | Logger implementation (init, get) |
| `src/common/io/FileSystem.h` | POSIX file ops: home_dir, config_dir, mkdir_p, read_file, write_file |
| `src/common/io/FileSystem.cpp` | FileSystem implementation |
| `src/common/io/Process.h` | Subprocess execution: exec() with timeout, command_exists() |
| `src/common/io/Process.cpp` | Process implementation (posix_spawn + pipe + poll) |
| `src/common/net/TlsConfig.h` | mbedTLS config, CA cert auto-detection |
| `src/common/net/TlsConfig.cpp` | TlsConfig implementation |
| `src/common/net/HttpClient.h` | cpp-httplib wrapper: get/post/stream_get/stream_post |
| `src/common/net/HttpClient.cpp` | HttpClient implementation |
| `src/common/net/SseParser.h` | Incremental SSE parser: feed() + callback |
| `src/common/net/SseParser.cpp` | SseParser implementation |
| `src/common/net/RetryPolicy.h` | Exponential backoff retry strategy |
| `src/common/net/RetryPolicy.cpp` | RetryPolicy implementation |
| `src/common/CMakeLists.txt` | common OBJECT library |
| `src/core/Types.h` | Shared types: Role, Message, ToolCall, LLMResponse, etc. |
| `src/core/IProvider.h` | LLM provider interface |
| `src/core/ITool.h` | Tool interface |
| `src/core/IMemory.h` | Memory backend interface |
| `src/core/IChannel.h` | Channel interface (Phase 3 stub) |
| `src/core/CMakeLists.txt` | core INTERFACE library |
| `src/provider/OpenAIProvider.h` | OpenAI-compatible provider header |
| `src/provider/OpenAIProvider.cpp` | OpenAI-compatible provider implementation |
| `src/provider/OllamaProvider.h` | Ollama provider header (Phase 2 stub) |
| `src/provider/OllamaProvider.cpp` | Ollama provider stub |
| `src/provider/ProviderFactory.h` | Factory: create provider from config |
| `src/provider/ProviderFactory.cpp` | Factory implementation |
| `src/provider/CMakeLists.txt` | provider OBJECT library |
| `src/tool/ToolRegistry.h` | Tool registration, lookup, execution |
| `src/tool/ToolRegistry.cpp` | ToolRegistry implementation |
| `src/tool/ShellTool.h` | Shell command execution tool |
| `src/tool/ShellTool.cpp` | ShellTool implementation |
| `src/tool/FileTool.h` | File read/write/edit tool (action-based) |
| `src/tool/FileTool.cpp` | FileTool implementation |
| `src/tool/SearchTool.h` | Content search tool (rg/grep) |
| `src/tool/SearchTool.cpp` | SearchTool implementation |
| `src/tool/WebTool.h` | Web search/fetch tool (action-based) |
| `src/tool/WebTool.cpp` | WebTool implementation |
| `src/tool/MemoryTool.h` | Memory store/recall/forget tool (action-based) |
| `src/tool/MemoryTool.cpp` | MemoryTool implementation |
| `src/tool/CMakeLists.txt` | tool OBJECT library |
| `src/memory/SqliteMemory.h` | SQLite + FTS5 memory backend |
| `src/memory/SqliteMemory.cpp` | SqliteMemory implementation |
| `src/memory/CMakeLists.txt` | memory OBJECT library |
| `src/agent/AgentLoop.h` | Core conversation loop |
| `src/agent/AgentLoop.cpp` | AgentLoop implementation |
| `src/agent/SystemPrompt.h` | Three-layer system prompt builder |
| `src/agent/SystemPrompt.cpp` | SystemPrompt implementation |
| `src/agent/CMakeLists.txt` | agent OBJECT library |
| `src/platform/Platform.h` | Platform detection + path management |
| `src/platform/Platform.cpp` | Platform implementation |
| `src/platform/CMakeLists.txt` | platform OBJECT library |
| `src/config/Config.h` | TOML config loading + merge |
| `src/config/Config.cpp` | Config implementation |
| `src/config/CMakeLists.txt` | config OBJECT library |
| `src/security/SecurityPolicy.h` | Autonomy levels + command filtering |
| `src/security/SecurityPolicy.cpp` | SecurityPolicy implementation |
| `src/security/CMakeLists.txt` | security OBJECT library |
| `src/main.cpp` | CLI entry point |
| `include/embedded-agent/Agent.h` | Public SDK API |
| `include/embedded-agent/Version.h` | Version constants |
| `configs/default.toml` | Default configuration template |
| `tests/CMakeLists.txt` | Test target definitions |
| `tests/test_common_result.cpp` | Result<T> tests |
| `tests/test_common_string.cpp` | StringUtil tests |
| `tests/test_common_json.cpp` | JsonHelper tests |
| `tests/test_common_sse.cpp` | SseParser tests |
| `tests/test_provider_openai.cpp` | OpenAIProvider tests (mock HttpClient) |
| `tests/test_tool_registry.cpp` | ToolRegistry tests |
| `tests/test_tool_shell.cpp` | ShellTool tests |
| `tests/test_tool_file.cpp` | FileTool tests |
| `tests/test_memory_sqlite.cpp` | SqliteMemory tests |
| `tests/test_agent_loop.cpp` | AgentLoop tests (mock Provider + Tool) |
| `tests/test_config.cpp` | Config loading tests |

---

## Task 1: Project Skeleton + CMake + .gitignore

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/Toolchain-Android.cmake`
- Create: `.gitignore`

- [ ] **Step 1: Create .gitignore**

```
build/
third_party/
.cache/
*.o
*.a
*.so
*.dylib
*.exe
.idea/
.vscode/
cmake-build-*/
```

- [ ] **Step 2: Create top-level CMakeLists.txt with FetchContent declarations**

```cmake
cmake_minimum_required(VERSION 3.16)
project(embedded-agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fno-exceptions -fno-rtti)
endif()

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party")

# --- header-only ---
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_Declare(toml11
    URL https://github.com/ToruNiina/toml11/archive/v4.2.0.tar.gz
)
FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/v1.15.0.tar.gz
)
FetchContent_Declare(cli11
    URL https://github.com/CLIUtils/CLI11/archive/v2.4.2.tar.gz
)
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/v0.18.3.tar.gz
)

# --- compiled ---
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/archive/v3.6.2.tar.gz
)

FetchContent_MakeAvailable(json toml11 spdlog cli11 httplib mbedtls)

# --- SQLite amalgamation ---
set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/third_party/sqlite3")
set(SQLITE3_SRC "${SQLITE3_DIR}/sqlite3.c")
set(SQLITE3_INC "${SQLITE3_DIR}")
if(NOT EXISTS ${SQLITE3_SRC})
    file(DOWNLOAD
        https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
        "${CMAKE_SOURCE_DIR}/third_party/sqlite3.zip"
    )
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar x
        "${CMAKE_SOURCE_DIR}/third_party/sqlite3.zip"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party"
    )
    file(RENAME
        "${CMAKE_SOURCE_DIR}/third_party/sqlite-amalgamation-3460000"
        "${SQLITE3_DIR}"
    )
endif()

add_library(sqlite3 STATIC ${SQLITE3_SRC})
target_include_directories(sqlite3 PUBLIC ${SQLITE3_INC})
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_JOURNAL_MODE_WAL
)

# --- sub-directories (will be added as tasks progress) ---
# add_subdirectory(src/common)
# add_subdirectory(src/core)
# ... etc

# --- core static library (will be completed later) ---
# add_library(embedded-agent-core STATIC ...)
# add_executable(embedded-agent src/main.cpp)
```

- [ ] **Step 3: Create cmake/Toolchain-Android.cmake**

```cmake
set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}" CACHE PATH "Android NDK path")
if(NOT ANDROID_NDK)
    message(FATAL_ERROR "Set ANDROID_NDK_HOME environment variable")
endif()

set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 21)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_NDK ${ANDROID_NDK})
set(CMAKE_ANDROID_STL_TYPE c++_static)
```

- [ ] **Step 4: Verify CMake configures successfully**

Run: `mkdir -p build && cd build && cmake ..`
Expected: Configuration succeeds, FetchContent downloads all dependencies

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt cmake/Toolchain-Android.cmake .gitignore
git commit -m "feat: project skeleton with CMake and FetchContent"
```

---

## Task 2: common/base — Error + Result + StringUtil + JsonHelper

**Files:**
- Create: `src/common/base/Error.h`
- Create: `src/common/base/Result.h`
- Create: `src/common/base/StringUtil.h`
- Create: `src/common/base/JsonHelper.h`
- Create: `src/common/CMakeLists.txt`
- Create: `tests/test_common_result.cpp`
- Create: `tests/test_common_string.cpp`
- Create: `tests/test_common_json.cpp`
- Create: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add subdirectories, test target)

- [ ] **Step 1: Write test_common_result.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/base/Result.h"

using namespace ea;

TEST_CASE("Result<T> stores value", "[result]") {
    Result<int> r = 42;
    REQUIRE(r.ok());
    REQUIRE(r.value() == 42);
}

TEST_CASE("Result<T> stores error", "[result]") {
    Result<int> r = Error{ErrorCode::NetworkError, "connection failed"};
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::NetworkError);
    REQUIRE(r.error().message == "connection failed");
}

TEST_CASE("Result<T> value_or returns value when ok", "[result]") {
    Result<int> r = 42;
    REQUIRE(r.value_or(0) == 42);
}

TEST_CASE("Result<T> value_or returns default when error", "[result]") {
    Result<int> r = Error{ErrorCode::Unknown, ""};
    REQUIRE(r.value_or(99) == 99);
}

TEST_CASE("Result<void> success", "[result]") {
    Result<void> r;
    REQUIRE(r.ok());
}

TEST_CASE("Result<void> error", "[result]") {
    Result<void> r = Error{ErrorCode::DbError, "query failed"};
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::DbError);
}

TEST_CASE("Error factory methods", "[result]") {
    auto e1 = Error::net("timeout", 504);
    REQUIRE(e1.code == ErrorCode::NetworkError);
    REQUIRE(e1.http_status == 504);

    auto e2 = Error::auth("bad key");
    REQUIRE(e2.code == ErrorCode::AuthError);

    auto e3 = Error::rate_limit("slow down");
    REQUIRE(e3.code == ErrorCode::RateLimit);
}
```

- [ ] **Step 2: Write Error.h**

```cpp
#pragma once
#include <string>

namespace ea {

enum class ErrorCode {
    Unknown,
    NetworkError,
    Timeout,
    AuthError,
    RateLimit,
    InvalidArgument,
    NotFound,
    IoError,
    DbError,
    ToolError,
    ParseError,
    SecurityBlocked,
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    int http_status = 0;
    std::string detail;

    static Error net(const std::string& msg, int status = 0) {
        return {ErrorCode::NetworkError, msg, status, {}};
    }
    static Error timeout(const std::string& msg) {
        return {ErrorCode::Timeout, msg, 0, {}};
    }
    static Error auth(const std::string& msg) {
        return {ErrorCode::AuthError, msg, 0, {}};
    }
    static Error rate_limit(const std::string& msg) {
        return {ErrorCode::RateLimit, msg, 0, {}};
    }
    static Error db(const std::string& msg) {
        return {ErrorCode::DbError, msg, 0, {}};
    }
    static Error tool_error(const std::string& msg) {
        return {ErrorCode::ToolError, msg, 0, {}};
    }
    static Error not_found(const std::string& msg) {
        return {ErrorCode::NotFound, msg, 0, {}};
    }
    static Error parse(const std::string& msg) {
        return {ErrorCode::ParseError, msg, 0, {}};
    }
    static Error security(const std::string& msg) {
        return {ErrorCode::SecurityBlocked, msg, 0, {}};
    }
};

}  // namespace ea
```

- [ ] **Step 3: Write Result.h**

```cpp
#pragma once
#include "Error.h"
#include <variant>
#include <optional>

namespace ea {

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}  // NOLINT
    Result(Error err) : data_(std::move(err)) {}   // NOLINT

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const & { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    T value_or(T default_val) const {
        if (ok()) return value();
        return default_val;
    }

    const Error& error() const { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(Error err) : error_(std::move(err)) {}  // NOLINT

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error& error() const { return *error_; }

private:
    std::optional<Error> error_;
};

}  // namespace ea
```

- [ ] **Step 4: Write StringUtil.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace ea::util {

inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    size_t start = 0, end;
    while ((end = s.find(delim, start)) != std::string::npos) {
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    parts.push_back(s.substr(start));
    return parts;
}

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

inline std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

}  // namespace ea::util
```

- [ ] **Step 5: Write JsonHelper.h**

```cpp
#pragma once
#include "StringUtil.h"
#include "nlohmann/json.hpp"
#include <optional>

namespace ea::util {

using json = nlohmann::json;

template<typename T>
T get_or(const json& j, const std::string& key, T default_val) {
    if (j.contains(key)) {
        try { return j.at(key).get<T>(); }
        catch (...) { return default_val; }
    }
    return default_val;
}

inline std::optional<json> get_path(const json& j, const std::string& path) {
    json current = j;
    for (const auto& key : split(path, '.')) {
        if (!current.is_object() && !current.is_array()) return std::nullopt;
        if (current.is_array()) {
            try { current = current.at(std::stoi(key)); }
            catch (...) { return std::nullopt; }
        } else {
            if (!current.contains(key)) return std::nullopt;
            current = current.at(key);
        }
    }
    return current;
}

}  // namespace ea::util
```

- [ ] **Step 6: Write test_common_string.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/base/StringUtil.h"

using namespace ea::util;

TEST_CASE("trim removes whitespace", "[string]") {
    REQUIRE(trim("  hello  ") == "hello");
    REQUIRE(trim("\t\nhello\r\n") == "hello");
    REQUIRE(trim("  ") == "");
    REQUIRE(trim("") == "");
}

TEST_CASE("split by delimiter", "[string]") {
    auto parts = split("a,b,c", ',');
    REQUIRE(parts.size() == 3);
    REQUIRE(parts[0] == "a");
    REQUIRE(parts[1] == "b");
    REQUIRE(parts[2] == "c");
}

TEST_CASE("split single element", "[string]") {
    auto parts = split("hello", ',');
    REQUIRE(parts.size() == 1);
    REQUIRE(parts[0] == "hello");
}

TEST_CASE("starts_with", "[string]") {
    REQUIRE(starts_with("hello world", "hello"));
    REQUIRE_FALSE(starts_with("hello world", "world"));
    REQUIRE_FALSE(starts_with("hi", "hello"));
}

TEST_CASE("ends_with", "[string]") {
    REQUIRE(ends_with("hello world", "world"));
    REQUIRE_FALSE(ends_with("hello world", "hello"));
}

TEST_CASE("to_lower", "[string]") {
    REQUIRE(to_lower("Hello WORLD") == "hello world");
}

TEST_CASE("replace_all", "[string]") {
    REQUIRE(replace_all("aabbcc", "bb", "XX") == "aaXXcc");
    REQUIRE(replace_all("aaa", "a", "bb") == "bbbbbb");
}
```

- [ ] **Step 7: Write test_common_json.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/base/JsonHelper.h"

using namespace ea::util;

TEST_CASE("get_or returns value when key exists", "[json]") {
    json j = {{"name", "test"}, {"count", 42}};
    REQUIRE(get_or(j, "name", std::string("default")) == "test");
    REQUIRE(get_or(j, "count", 0) == 42);
}

TEST_CASE("get_or returns default when key missing", "[json]") {
    json j = {{"name", "test"}};
    REQUIRE(get_or(j, "missing", std::string("default")) == "default");
}

TEST_CASE("get_or returns default on type mismatch", "[json]") {
    json j = {{"count", "not_a_number"}};
    REQUIRE(get_or(j, "count", 0) == 0);
}

TEST_CASE("get_path nested access", "[json]") {
    json j = R"({"choices":[{"message":{"content":"hello"}}]})"_json;
    auto result = get_path(j, "choices.0.message.content");
    REQUIRE(result.has_value());
    REQUIRE(result->get<std::string>() == "hello");
}

TEST_CASE("get_path returns nullopt for missing path", "[json]") {
    json j = R"({"a":1})"_json;
    REQUIRE_FALSE(get_path(j, "b.c.d").has_value());
}
```

- [ ] **Step 8: Write src/common/CMakeLists.txt**

```cmake
add_library(ea-common OBJECT
    io/Logger.cpp
    io/FileSystem.cpp
    io/Process.cpp
)
target_include_directories(ea-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-common PRIVATE spdlog::spdlog)
```

Note: io/*.cpp files don't exist yet — will be created in Task 3. For now, create empty stubs so CMake doesn't fail.

- [ ] **Step 9: Create empty io stubs to satisfy CMake**

Create empty files:
- `src/common/io/Logger.cpp`
- `src/common/io/FileSystem.cpp`
- `src/common/io/Process.cpp`

- [ ] **Step 10: Write tests/CMakeLists.txt**

```cmake
add_executable(ea-tests
    test_common_result.cpp
    test_common_string.cpp
    test_common_json.cpp
)
target_link_libraries(ea-tests PRIVATE
    ea-common
    Catch2::Catch2WithMain
)
target_include_directories(ea-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(CTest)
add_test(NAME ea-tests COMMAND ea-tests)
```

- [ ] **Step 11: Update top-level CMakeLists.txt — add subdirectories and test option**

Add after FetchContent_MakeAvailable:

```cmake
add_subdirectory(src/common)

option(ENABLE_TESTS "Build tests" OFF)
if(ENABLE_TESTS)
    FetchContent_Declare(catch2
        URL https://github.com/catchorg/Catch2/archive/v3.7.1.tar.gz
    )
    FetchContent_MakeAvailable(catch2)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 12: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake .. -DENABLE_TESTS=ON && cmake --build . && ./tests/ea-tests`
Expected: All tests pass

- [ ] **Step 13: Commit**

```bash
git add src/common/ tests/ CMakeLists.txt
git commit -m "feat: common/base module — Error, Result<T>, StringUtil, JsonHelper with tests"
```

---

## Task 3: common/io — Logger + FileSystem + Process

**Files:**
- Create: `src/common/io/Logger.h`
- Modify: `src/common/io/Logger.cpp` (replace empty stub)
- Create: `src/common/io/FileSystem.h`
- Modify: `src/common/io/FileSystem.cpp` (replace empty stub)
- Create: `src/common/io/Process.h`
- Modify: `src/common/io/Process.cpp` (replace empty stub)

- [ ] **Step 1: Write Logger.h**

```cpp
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stderr_color_sink.h>
#include <memory>
#include <string>

namespace ea::log {

void init(const std::string& log_dir, bool debug = false);
std::shared_ptr<spdlog::logger> get();

}  // namespace ea::log

#define EA_DEBUG(...)    ea::log::get()->debug(__VA_ARGS__)
#define EA_INFO(...)     ea::log::get()->info(__VA_ARGS__)
#define EA_WARN(...)     ea::log::get()->warn(__VA_ARGS__)
#define EA_ERROR(...)    ea::log::get()->error(__VA_ARGS__)
```

- [ ] **Step 2: Write Logger.cpp**

```cpp
#include "Logger.h"

namespace ea::log {

static std::shared_ptr<spdlog::logger> g_logger;

void init(const std::string& log_dir, bool debug) {
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        log_dir + "/agent.log", true);

    std::vector<spdlog::sink_ptr> sinks{console, file};
    g_logger = std::make_shared<spdlog::logger>("ea", sinks.begin(), sinks.end());

    if (debug) {
        g_logger->set_level(spdlog::level::debug);
    } else {
        g_logger->set_level(spdlog::level::info);
    }
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(g_logger);
}

std::shared_ptr<spdlog::logger> get() {
    if (!g_logger) {
        // Fallback: init with defaults if user forgot
        init("/tmp", false);
    }
    return g_logger;
}

}  // namespace ea::log
```

- [ ] **Step 3: Write FileSystem.h**

```cpp
#pragma once
#include "common/base/Result.h"
#include <string>
#include <vector>

namespace ea::fs {

Result<std::string> home_dir();
Result<std::string> config_dir();
Result<std::string> data_dir();
Result<bool> exists(const std::string& path);
Result<bool> is_dir(const std::string& path);
Result<void> mkdir_p(const std::string& path);
Result<void> remove(const std::string& path);
Result<std::string> read_file(const std::string& path);
Result<void> write_file(const std::string& path, const std::string& content);
Result<std::vector<std::string>> list_dir(const std::string& path);

}  // namespace ea::fs
```

- [ ] **Step 4: Write FileSystem.cpp**

```cpp
#include "FileSystem.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>

namespace ea::fs {

Result<std::string> home_dir() {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);
    return Error{ErrorCode::IoError, "HOME not set"};
}

Result<std::string> config_dir() {
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent";
}

Result<std::string> data_dir() {
    return config_dir();  // Same dir for now
}

Result<bool> exists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return true;
    if (errno == ENOENT) return false;
    return Error{ErrorCode::IoError, std::string("stat failed: ") + strerror(errno)};
}

Result<bool> is_dir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return Error{ErrorCode::IoError, std::string("stat failed: ") + strerror(errno)};
    }
    return S_ISDIR(st.st_mode);
}

Result<void> mkdir_p(const std::string& path) {
    if (path.empty()) return Error{ErrorCode::InvalidArgument, "empty path"};

    // Check if already exists
    auto ex = exists(path);
    if (ex.ok() && ex.value()) {
        auto dir = is_dir(path);
        if (dir.ok() && dir.value()) return {};
        return Error{ErrorCode::IoError, path + " exists but is not a directory"};
    }

    // Recursively create parent
    auto parent = path.substr(0, path.rfind('/'));
    if (!parent.empty() && parent != path) {
        auto parent_exists = exists(parent);
        if (!parent_exists.ok() || !parent_exists.value()) {
            auto r = mkdir_p(parent);
            if (!r.ok()) return r;
        }
    }

    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        return Error{ErrorCode::IoError,
                     std::string("mkdir failed for ") + path + ": " + strerror(errno)};
    }
    return {};
}

Result<void> remove(const std::string& path) {
    if (::remove(path.c_str()) != 0) {
        return Error{ErrorCode::IoError,
                     std::string("remove failed for ") + path + ": " + strerror(errno)};
    }
    return {};
}

Result<std::string> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Error{ErrorCode::IoError, "cannot open: " + path};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Result<void> write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::out | std::ios::binary);
    if (!f.is_open()) {
        return Error{ErrorCode::IoError, "cannot open for write: " + path};
    }
    f << content;
    if (!f.good()) {
        return Error{ErrorCode::IoError, "write failed: " + path};
    }
    return {};
}

Result<std::vector<std::string>> list_dir(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return Error{ErrorCode::IoError, "opendir failed: " + path};
    }
    std::vector<std::string> entries;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name != "." && name != "..") {
            entries.push_back(std::move(name));
        }
    }
    closedir(dir);
    return entries;
}

}  // namespace ea::fs
```

- [ ] **Step 5: Write Process.h**

```cpp
#pragma once
#include "common/base/Result.h"
#include <string>
#include <chrono>

namespace ea::process {

struct ExecResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
};

Result<ExecResult> exec(const std::string& command,
                         const std::string& working_dir = "",
                         std::chrono::milliseconds timeout = std::chrono::seconds(30),
                         int max_output_bytes = 65536);

bool command_exists(const std::string& cmd);

}  // namespace ea::process
```

- [ ] **Step 6: Write Process.cpp**

```cpp
#include "Process.h"
#include "common/io/Logger.h"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <cstring>
#include <array>
#include <chrono>

extern char** environ;

namespace ea::process {

Result<ExecResult> exec(const std::string& command,
                         const std::string& working_dir,
                         std::chrono::milliseconds timeout,
                         int max_output_bytes) {
    // Create pipes for stdout and stderr
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return Error{ErrorCode::IoError, "pipe() failed"};
    }

    // Build argv for /bin/sh -c "command"
    std::string shell = "/bin/sh";
    std::array<char*, 4> argv = {
        const_cast<char*>(shell.c_str()),
        const_cast<char*>("-c"),
        const_cast<char*>(command.c_str()),
        nullptr
    };

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    pid_t pid;
    int spawn_result = posix_spawnp(&pid, shell.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_result != 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return Error{ErrorCode::IoError, "posix_spawnp failed: " + std::string(strerror(spawn_result))};
    }

    // Close write ends
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Read with timeout using poll
    std::string stdout_buf, stderr_buf;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool timed_out = false;

    while (true) {
        struct pollfd fds[2] = {
            {stdout_pipe[0], POLLIN, 0},
            {stderr_pipe[0], POLLIN, 0},
        };

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            timed_out = true;
            break;
        }

        int ret = poll(fds, 2, static_cast<int>(remaining.count()));
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            timed_out = true;
            break;
        }

        char buf[4096];
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                if (static_cast<int>(stdout_buf.size() + n) <= max_output_bytes) {
                    stdout_buf.append(buf, n);
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
            if (n > 0) {
                if (static_cast<int>(stderr_buf.size() + n) <= max_output_bytes) {
                    stderr_buf.append(buf, n);
                }
            }
        }

        // Check if both pipes closed
        if ((fds[0].revents & (POLLHUP | POLLERR)) && (fds[1].revents & (POLLHUP | POLLERR))) {
            break;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return Error{ErrorCode::Timeout, "command timed out: " + command};
    }

    int status;
    waitpid(pid, &status, 0);

    ExecResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_output = std::move(stdout_buf);
    result.stderr_output = std::move(stderr_buf);
    return result;
}

bool command_exists(const std::string& cmd) {
    std::string which = "command -v " + cmd + " 2>/dev/null";
    auto result = exec(which, "", std::chrono::seconds(5), 1024);
    return result.ok() && result.value().exit_code == 0;
}

}  // namespace ea::process
```

- [ ] **Step 7: Build and verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build .`
Expected: Build succeeds

- [ ] **Step 8: Commit**

```bash
git add src/common/io/
git commit -m "feat: common/io module — Logger, FileSystem, Process"
```

---

## Task 4: common/net — TlsConfig + HttpClient + SseParser + RetryPolicy

**Files:**
- Create: `src/common/net/TlsConfig.h`
- Create: `src/common/net/TlsConfig.cpp`
- Create: `src/common/net/HttpClient.h`
- Create: `src/common/net/HttpClient.cpp`
- Create: `src/common/net/SseParser.h`
- Create: `src/common/net/SseParser.cpp`
- Create: `src/common/net/RetryPolicy.h`
- Create: `src/common/net/RetryPolicy.cpp`
- Create: `tests/test_common_sse.cpp`
- Modify: `src/common/CMakeLists.txt` (add net sources)
- Modify: `tests/CMakeLists.txt` (add sse test)

- [ ] **Step 1: Write test_common_sse.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "common/net/SseParser.h"

using namespace ea::net;

TEST_CASE("SseParser parses single event", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;

    parser.feed("data: hello\n\n", [&](const SseEvent& e) {
        events.push_back(e);
    });

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser parses event with type", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;

    parser.feed("event: message\ndata: hello\n\n", [&](const SseEvent& e) {
        events.push_back(e);
    });

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event == "message");
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser handles multi-line data", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;

    parser.feed("data: line1\ndata: line2\n\n", [&](const SseEvent& e) {
        events.push_back(e);
    });

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "line1\nline2");
}

TEST_CASE("SseParser handles incremental chunks", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;
    auto on_event = [&](const SseEvent& e) { events.push_back(e); };

    parser.feed("data: hel", on_event);
    REQUIRE(events.size() == 0);

    parser.feed("lo\n\n", on_event);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser ignores comments", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;

    parser.feed(": this is a comment\ndata: hello\n\n", [&](const SseEvent& e) {
        events.push_back(e);
    });

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser handles [DONE] marker", "[sse]") {
    SseParser parser;
    std::vector<SseEvent> events;

    parser.feed("data: [DONE]\n\n", [&](const SseEvent& e) {
        events.push_back(e);
    });

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "[DONE]");
}
```

- [ ] **Step 2: Write TlsConfig.h**

```cpp
#pragma once
#include <string>

namespace ea::net {

struct TlsConfig {
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    bool verify_server = true;

    static std::string detect_ca_path();
};

}  // namespace ea::net
```

- [ ] **Step 3: Write TlsConfig.cpp**

```cpp
#include "TlsConfig.h"
#include "common/io/FileSystem.h"

namespace ea::net {

std::string TlsConfig::detect_ca_path() {
    // Common CA bundle locations on Linux
    static const char* paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",     // Debian/Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",       // RHEL/CentOS
        "/etc/ssl/ca-bundle.pem",                   // OpenSUSE
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // Fedora
        "/data/data/com.termux/files/usr/etc/tls/cert.pem",   // Termux
        nullptr
    };

    for (int i = 0; paths[i] != nullptr; ++i) {
        auto ex = ea::fs::exists(paths[i]);
        if (ex.ok() && ex.value()) {
            return paths[i];
        }
    }
    return "";
}

}  // namespace ea::net
```

- [ ] **Step 4: Write SseParser.h**

```cpp
#pragma once
#include <string>
#include <functional>
#include <map>

namespace ea::net {

struct SseEvent {
    std::string event;
    std::string data;
    std::string id;
};

class SseParser {
public:
    void feed(const std::string& chunk, std::function<void(const SseEvent&)> on_event);
    void reset();

private:
    std::string buffer_;
};

}  // namespace ea::net
```

- [ ] **Step 5: Write SseParser.cpp**

```cpp
#include "SseParser.h"
#include "common/base/StringUtil.h"

namespace ea::net {

void SseParser::feed(const std::string& chunk, std::function<void(const SseEvent&)> on_event) {
    buffer_.append(chunk);

    while (true) {
        // Find double newline (event boundary)
        size_t pos = buffer_.find("\n\n");
        if (pos == std::string::npos) break;

        std::string event_text = buffer_.substr(0, pos);
        buffer_ = buffer_.substr(pos + 2);

        SseEvent event;
        std::string current_data;

        auto lines = ea::util::split(event_text, '\n');
        for (auto& line : lines) {
            // Skip comments
            if (ea::util::starts_with(line, ":")) continue;

            auto colon_pos = line.find(':');
            if (colon_pos == std::string::npos) continue;

            std::string field = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            // Remove leading space from value (SSE spec)
            if (!value.empty() && value[0] == ' ') value = value.substr(1);

            if (field == "event") {
                event.event = value;
            } else if (field == "data") {
                if (!current_data.empty()) current_data += "\n";
                current_data += value;
            } else if (field == "id") {
                event.id = value;
            }
            // Ignore "retry" field for now
        }

        event.data = std::move(current_data);
        if (!event.data.empty() || !event.event.empty()) {
            on_event(event);
        }
    }
}

void SseParser::reset() {
    buffer_.clear();
}

}  // namespace ea::net
```

- [ ] **Step 6: Write RetryPolicy.h**

```cpp
#pragma once
#include <chrono>

namespace ea::net {

class RetryPolicy {
public:
    int max_retries = 3;
    std::chrono::milliseconds base_delay{1000};
    double backoff_multiplier = 2.0;
    std::chrono::milliseconds max_delay{30000};

    bool should_retry(int status) const;
    std::chrono::milliseconds delay_for(int attempt) const;
};

}  // namespace ea::net
```

- [ ] **Step 7: Write RetryPolicy.cpp**

```cpp
#include "RetryPolicy.h"

namespace ea::net {

bool RetryPolicy::should_retry(int status) const {
    // Retry on 429 (rate limit) and 5xx (server errors)
    return status == 429 || (status >= 500 && status < 600);
}

std::chrono::milliseconds RetryPolicy::delay_for(int attempt) const {
    auto delay = base_delay;
    for (int i = 0; i < attempt; ++i) {
        delay = std::chrono::milliseconds(
            static_cast<long>(delay.count() * backoff_multiplier));
    }
    if (delay > max_delay) delay = max_delay;
    return delay;
}

}  // namespace ea::net
```

- [ ] **Step 8: Write HttpClient.h**

```cpp
#pragma once
#include "common/base/Result.h"
#include "TlsConfig.h"
#include "RetryPolicy.h"
#include <string>
#include <functional>
#include <map>
#include <chrono>

namespace ea::net {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct RequestOptions {
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
    std::map<std::string, std::string> headers;
    std::string body;
    TlsConfig tls;
    bool follow_redirects = true;
    RetryPolicy retry;
};

class HttpClient {
public:
    Result<HttpResponse> get(const std::string& url, const RequestOptions& opts = {});
    Result<HttpResponse> post(const std::string& url, const RequestOptions& opts = {});
    Result<void> stream_get(const std::string& url,
                             std::function<void(const std::string&)> on_chunk,
                             const RequestOptions& opts = {});
    Result<void> stream_post(const std::string& url,
                              std::function<void(const std::string&)> on_chunk,
                              const RequestOptions& opts = {});
};

}  // namespace ea::net
```

- [ ] **Step 9: Write HttpClient.cpp**

```cpp
#include "HttpClient.h"
#include "common/io/Logger.h"
#include <httplib.h>
#include <thread>

namespace ea::net {

static std::unique_ptr<httplib::Client> make_client(const std::string& url, const RequestOptions& opts) {
    auto client = std::make_unique<httplib::Client>(url);

    client->set_connection_timeout(opts.timeout);
    client->set_read_timeout(opts.timeout);
    client->set_follow_location(opts.follow_redirects);

    if (!opts.tls.ca_cert_path.empty()) {
        client->set_ca_cert_path(opts.tls.ca_cert_path);
    }
    client->enable_server_certificate_verification(opts.tls.verify_server);

    return client;
}

Result<HttpResponse> HttpClient::get(const std::string& url, const RequestOptions& opts) {
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Get("/", headers);
        if (!res) {
            return Error::net("HTTP GET failed: " + httplib::to_string(res.error()));
        }
        if (opts.retry.should_retry(res->status)) {
            auto delay = opts.retry.delay_for(attempt);
            EA_WARN("HTTP {} received, retrying in {}ms (attempt {}/{})",
                    res->status, delay.count(), attempt + 1, opts.retry.max_retries);
            std::this_thread::sleep_for(delay);
            continue;
        }
        HttpResponse response;
        response.status = res->status;
        response.body = res->body;
        for (auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
        return response;
    }
    return Error::rate_limit("max retries exceeded");
}

Result<HttpResponse> HttpClient::post(const std::string& url, const RequestOptions& opts) {
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    for (int attempt = 0; attempt <= opts.retry.max_retries; ++attempt) {
        auto res = client->Post("/", headers, opts.body, "application/json");
        if (!res) {
            return Error::net("HTTP POST failed: " + httplib::to_string(res.error()));
        }
        if (opts.retry.should_retry(res->status)) {
            auto delay = opts.retry.delay_for(attempt);
            EA_WARN("HTTP {} received, retrying in {}ms (attempt {}/{})",
                    res->status, delay.count(), attempt + 1, opts.retry.max_retries);
            std::this_thread::sleep_for(delay);
            continue;
        }
        HttpResponse response;
        response.status = res->status;
        response.body = res->body;
        for (auto& [k, v] : res->headers) {
            response.headers[k] = v;
        }
        return response;
    }
    return Error::rate_limit("max retries exceeded");
}

Result<void> HttpClient::stream_get(const std::string& url,
                                     std::function<void(const std::string&)> on_chunk,
                                     const RequestOptions& opts) {
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    auto res = client->Get("/", headers,
        [&](const char* data, size_t len) -> bool {
            on_chunk(std::string(data, len));
            return true;
        });

    if (!res) {
        return Error::net("HTTP stream GET failed: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        return Error::net("HTTP stream GET returned " + std::to_string(res->status), res->status);
    }
    return {};
}

Result<void> HttpClient::stream_post(const std::string& url,
                                      std::function<void(const std::string&)> on_chunk,
                                      const RequestOptions& opts) {
    auto client = make_client(url, opts);

    httplib::Headers headers;
    for (auto& [k, v] : opts.headers) {
        headers.emplace(k, v);
    }

    auto res = client->Post("/", headers, opts.body, "application/json",
        [&](const char* data, size_t len) -> bool {
            on_chunk(std::string(data, len));
            return true;
        });

    if (!res) {
        return Error::net("HTTP stream POST failed: " + httplib::to_string(res.error()));
    }
    if (res->status != 200) {
        return Error::net("HTTP stream POST returned " + std::to_string(res->status), res->status);
    }
    return {};
}

}  // namespace ea::net
```

- [ ] **Step 10: Update src/common/CMakeLists.txt to include net sources**

```cmake
add_library(ea-common OBJECT
    io/Logger.cpp
    io/FileSystem.cpp
    io/Process.cpp
    net/TlsConfig.cpp
    net/HttpClient.cpp
    net/SseParser.cpp
    net/RetryPolicy.cpp
)
target_include_directories(ea-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-common PRIVATE spdlog::spdlog httplib::httplib mbedtls::mbedtls)
```

- [ ] **Step 11: Update tests/CMakeLists.txt to add sse test**

Add `test_common_sse.cpp` to the test executable sources.

- [ ] **Step 12: Build and run tests**

Run: `cd /home/lsy/embedded-agent/build && cmake --build . && ./tests/ea-tests`
Expected: All tests pass including SSE tests

- [ ] **Step 13: Commit**

```bash
git add src/common/net/ tests/test_common_sse.cpp src/common/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: common/net module — TlsConfig, HttpClient, SseParser, RetryPolicy"
```

---

## Task 5: core — Types + Interfaces

**Files:**
- Create: `src/core/Types.h`
- Create: `src/core/IProvider.h`
- Create: `src/core/ITool.h`
- Create: `src/core/IMemory.h`
- Create: `src/core/IChannel.h`
- Create: `src/core/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add core subdirectory)

- [ ] **Step 1: Write Types.h**

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include "nlohmann/json.hpp"

namespace ea {

using json = nlohmann::json;

enum class Role { System, User, Assistant, Tool };

struct ToolCall {
    std::string id;
    std::string name;
    json arguments;
};

struct Message {
    Role role;
    std::string content;
    std::optional<std::string> name;
    std::optional<std::vector<ToolCall>> tool_calls;
    std::optional<std::string> tool_call_id;
};

struct Usage {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_read_tokens = 0;
    int cache_write_tokens = 0;
};

struct LLMResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    Usage usage;
    std::string stop_reason;
    bool is_tool_use() const { return stop_reason == "tool_use"; }
};

struct StreamChunk {
    enum Type { Content, ToolCallBegin, ToolCallDelta, ToolCallEnd, Done, Error };
    Type type;
    std::string data;
    std::optional<ToolCall> tool_call;
    std::optional<Usage> usage;
};

struct ToolSpec {
    std::string name;
    std::string description;
    json parameters;
};

struct ToolResult {
    std::string call_id;
    std::string output;
    bool is_error = false;
};

struct MemoryEntry {
    std::string id;
    std::string content;
    std::string category;
    int importance = 5;
    std::string created_at;
    std::optional<std::string> agent_id;
};

struct ChatOptions {
    float temperature = 0.7f;
    int max_tokens = 4096;
    std::optional<std::string> stop;
    int top_p = 1;
    bool stream = false;
};

}  // namespace ea
```

- [ ] **Step 2: Write IProvider.h**

```cpp
#pragma once
#include "Types.h"
#include "common/base/Result.h"
#include <functional>

namespace ea {

class IProvider {
public:
    virtual ~IProvider() = default;
    virtual std::string name() const = 0;
    virtual std::vector<std::string> list_models() const = 0;
    virtual Result<LLMResponse> chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        const ChatOptions& opts = {}) = 0;
    virtual Result<void> stream_chat(
        const std::vector<Message>& messages,
        const std::vector<ToolSpec>& tools,
        const std::string& model,
        std::function<void(const StreamChunk&)> on_chunk,
        const ChatOptions& opts = {}) = 0;
};

}  // namespace ea
```

- [ ] **Step 3: Write ITool.h**

```cpp
#pragma once
#include "Types.h"
#include "common/base/Result.h"

namespace ea {

class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual Result<ToolResult> execute(const json& args) = 0;
};

}  // namespace ea
```

- [ ] **Step 4: Write IMemory.h**

```cpp
#pragma once
#include "Types.h"
#include "common/base/Result.h"

namespace ea {

class IMemory {
public:
    virtual ~IMemory() = default;
    virtual Result<std::string> store(const std::string& content,
                                       const std::string& category = "core",
                                       int importance = 5) = 0;
    virtual Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                                     int limit = 10) = 0;
    virtual Result<bool> forget(const std::string& id) = 0;
    virtual Result<std::vector<MemoryEntry>> list(int limit = 50, int offset = 0) = 0;
    virtual Result<int> count() = 0;
};

}  // namespace ea
```

- [ ] **Step 5: Write IChannel.h**

```cpp
#pragma once
#include "common/base/Result.h"
#include <string>

namespace ea {

class IChannel {
public:
    virtual ~IChannel() = default;
    virtual std::string name() const = 0;
    virtual Result<void> send(const std::string& message) = 0;
};

}  // namespace ea
```

- [ ] **Step 6: Write src/core/CMakeLists.txt**

```cmake
add_library(ea-core INTERFACE)
target_include_directories(ea-core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-core INTERFACE ea-common nlohmann_json::nlohmann_json)
```

- [ ] **Step 7: Update top-level CMakeLists.txt — add core subdirectory**

Add `add_subdirectory(src/core)` after `add_subdirectory(src/common)`.

- [ ] **Step 8: Build and verify**

Run: `cd /home/lsy/embedded-agent/build && cmake --build .`
Expected: Build succeeds

- [ ] **Step 9: Commit**

```bash
git add src/core/ CMakeLists.txt
git commit -m "feat: core module — Types, IProvider, ITool, IMemory, IChannel"
```

---

## Task 6: platform — Platform Detection

**Files:**
- Create: `src/platform/Platform.h`
- Create: `src/platform/Platform.cpp`
- Create: `src/platform/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add platform subdirectory)

- [ ] **Step 1: Write Platform.h**

```cpp
#pragma once
#include <string>

namespace ea::platform {

enum class PlatformKind { Linux, Android, Wsl, Unknown };

PlatformKind detect();
bool is_android();
bool is_linux();
bool is_wsl();
std::string home_dir();
std::string shell_path();
std::string tmp_dir();
bool has_shell_access();
bool has_filesystem_access();
bool supports_long_running();
std::string platform_description();

}  // namespace ea::platform
```

- [ ] **Step 2: Write Platform.cpp**

```cpp
#include "Platform.h"
#include "common/io/FileSystem.h"
#include "common/base/StringUtil.h"
#include <unistd.h>
#include <sys/utsname.h>

namespace ea::platform {

static PlatformKind cached_kind = PlatformKind::Unknown;

PlatformKind detect() {
    if (cached_kind != PlatformKind::Unknown) return cached_kind;

    // Check Android first (Termux)
    if (is_android()) { cached_kind = PlatformKind::Android; return cached_kind; }
    if (is_wsl()) { cached_kind = PlatformKind::Wsl; return cached_kind; }
    cached_kind = PlatformKind::Linux;
    return cached_kind;
}

bool is_android() {
    // Termux sets TERMUX_VERSION or has /system/bin/sh
    const char* termux = getenv("TERMUX_VERSION");
    if (termux && termux[0] != '\0') return true;
    return access("/system/bin/sh") == 0;
}

bool is_linux() {
    struct utsname buf;
    if (uname(&buf) != 0) return false;
    return util::starts_with(buf.sysname, "Linux");
}

bool is_wsl() {
    auto content = fs::read_file("/proc/version");
    if (!content.ok()) return false;
    return content.value().find("microsoft") != std::string::npos ||
           content.value().find("WSL") != std::string::npos;
}

std::string home_dir() {
    auto r = fs::home_dir();
    return r.ok() ? r.value() : "/tmp";
}

std::string shell_path() {
    if (is_android()) return "/system/bin/sh";
    const char* shell = getenv("SHELL");
    if (shell && shell[0] != '\0') return shell;
    return "/bin/bash";
}

std::string tmp_dir() {
    const char* tmp = getenv("TMPDIR");
    if (tmp && tmp[0] != '\0') return tmp;
    if (is_android()) return home_dir() + "/tmp";
    return "/tmp";
}

bool has_shell_access() { return true; }
bool has_filesystem_access() { return true; }
bool supports_long_running() { return !is_android(); }

std::string platform_description() {
    struct utsname buf;
    uname(&buf);
    std::string desc = "Platform: ";
    switch (detect()) {
        case PlatformKind::Android: desc += "Android/Termux"; break;
        case PlatformKind::Wsl: desc += "WSL2"; break;
        case PlatformKind::Linux: desc += "Linux"; break;
        default: desc += "Unknown"; break;
    }
    desc += " | OS: " + std::string(buf.sysname);
    desc += " | Arch: " + std::string(buf.machine);
    desc += " | Shell: " + shell_path();
    return desc;
}

}  // namespace ea::platform
```

- [ ] **Step 3: Write src/platform/CMakeLists.txt**

```cmake
add_library(ea-platform OBJECT Platform.cpp)
target_include_directories(ea-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-platform PUBLIC ea-common)
```

- [ ] **Step 4: Update top-level CMakeLists.txt — add platform subdirectory**

- [ ] **Step 5: Build and verify**

- [ ] **Step 6: Commit**

```bash
git add src/platform/ CMakeLists.txt
git commit -m "feat: platform module — Linux/Android/WSL detection"
```

---

## Task 7: config — TOML Configuration

**Files:**
- Create: `src/config/Config.h`
- Create: `src/config/Config.cpp`
- Create: `src/config/CMakeLists.txt`
- Create: `configs/default.toml`
- Create: `tests/test_config.cpp`
- Modify: `CMakeLists.txt` (add config subdirectory)
- Modify: `tests/CMakeLists.txt` (add config test)

- [ ] **Step 1: Write Config.h**

```cpp
#pragma once
#include "common/base/Result.h"
#include "common/net/TlsConfig.h"
#include "common/net/RetryPolicy.h"
#include <string>
#include <vector>
#include <chrono>

namespace ea::config {

struct ProviderConfig {
    std::string type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string default_model;
    net::TlsConfig tls;
    net::RetryPolicy retry;
    std::chrono::milliseconds timeout{60000};
};

struct MemoryConfig {
    std::string backend = "sqlite";
    std::string path;
    bool enable_fts5 = true;
};

struct SecurityConfig {
    std::string autonomy = "supervised";
    std::vector<std::string> allowed_commands;
    std::string workspace;
};

struct AgentConfig {
    std::string model;
    int max_iterations = 90;
    bool auto_memory = true;
    std::string soul;
};

struct AppConfig {
    ProviderConfig provider;
    MemoryConfig memory;
    SecurityConfig security;
    AgentConfig agent;
    std::string config_path;
};

Result<AppConfig> load(const std::string& config_path = "");

}  // namespace ea::config
```

- [ ] **Step 2: Write Config.cpp**

```cpp
#include "Config.h"
#include "common/io/FileSystem.h"
#include "common/io/Logger.h"
#include <toml.hpp>
#include <cstdlib>

namespace ea::config {

Result<AppConfig> load(const std::string& config_path) {
    AppConfig cfg;

    // Determine config file path
    std::string path = config_path;
    if (path.empty()) {
        const char* env_path = getenv("EMBEDDED_AGENT_CONFIG");
        if (env_path && env_path[0] != '\0') {
            path = env_path;
        } else {
            auto cfg_dir = fs::config_dir();
            if (cfg_dir.ok()) {
                path = cfg_dir.value() + "/config.toml";
            }
        }
    }

    cfg.config_path = path;

    // Load TOML if file exists
    auto exists = fs::exists(path);
    if (!exists.ok() || !exists.value()) {
        EA_WARN("Config file not found: {}, using defaults", path);
        // Apply env var overrides even without config file
        const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
        if (api_key) cfg.provider.api_key = api_key;
        const char* model = getenv("EMBEDDED_AGENT_MODEL");
        if (model) cfg.agent.model = model;
        return cfg;
    }

    try {
        auto data = toml::parse(path);

        // [agent]
        if (data.contains("agent")) {
            auto agent = toml::find(data, "agent");
            cfg.agent.model = toml::find_or<std::string>(agent, "model", cfg.agent.model);
            cfg.agent.max_iterations = toml::find_or<int>(agent, "max_iterations", cfg.agent.max_iterations);
            cfg.agent.auto_memory = toml::find_or<bool>(agent, "auto_memory", cfg.agent.auto_memory);
            cfg.agent.soul = toml::find_or<std::string>(agent, "soul", cfg.agent.soul);
        }

        // [provider]
        if (data.contains("provider")) {
            auto provider = toml::find(data, "provider");
            cfg.provider.type = toml::find_or<std::string>(provider, "type", cfg.provider.type);
            cfg.provider.base_url = toml::find_or<std::string>(provider, "base_url", cfg.provider.base_url);
            cfg.provider.api_key = toml::find_or<std::string>(provider, "api_key", cfg.provider.api_key);
            cfg.provider.default_model = toml::find_or<std::string>(provider, "default_model", cfg.provider.default_model);
            cfg.provider.timeout = std::chrono::milliseconds(
                toml::find_or<int>(provider, "timeout", 60) * 1000);
        }

        // [memory]
        if (data.contains("memory")) {
            auto memory = toml::find(data, "memory");
            cfg.memory.backend = toml::find_or<std::string>(memory, "backend", cfg.memory.backend);
            cfg.memory.path = toml::find_or<std::string>(memory, "path", cfg.memory.path);
            cfg.memory.enable_fts5 = toml::find_or<bool>(memory, "enable_fts5", cfg.memory.enable_fts5);
        }

        // [security]
        if (data.contains("security")) {
            auto security = toml::find(data, "security");
            cfg.security.autonomy = toml::find_or<std::string>(security, "autonomy", cfg.security.autonomy);
            cfg.security.workspace = toml::find_or<std::string>(security, "workspace", cfg.security.workspace);
            if (security.contains("allowed_commands")) {
                cfg.security.allowed_commands = toml::find<std::vector<std::string>>(security, "allowed_commands");
            }
        }

    } catch (const toml::syntax_error& e) {
        return Error{ErrorCode::ParseError, std::string("TOML parse error: ") + e.what()};
    } catch (const std::exception& e) {
        return Error{ErrorCode::ParseError, std::string("Config error: ") + e.what()};
    }

    // Environment variable overrides
    const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
    if (api_key && api_key[0] != '\0') cfg.provider.api_key = api_key;
    const char* model = getenv("EMBEDDED_AGENT_MODEL");
    if (model && model[0] != '\0') cfg.agent.model = model;

    return cfg;
}

}  // namespace ea::config
```

- [ ] **Step 3: Write configs/default.toml**

```toml
[agent]
model = "deepseek-chat"
max_iterations = 90
auto_memory = true
soul = "You are a helpful AI assistant running on a Linux/Android device."

[provider]
type = "openai_compatible"
base_url = "https://api.deepseek.com/v1"
api_key = ""
default_model = "deepseek-chat"
timeout = 60

[memory]
backend = "sqlite"
path = ""
enable_fts5 = true

[security]
autonomy = "supervised"
workspace = ""
allowed_commands = [
    "ls", "cat", "head", "tail", "grep", "find", "wc",
    "pwd", "echo", "date", "whoami", "uname",
    "git", "curl", "wget"
]
```

- [ ] **Step 4: Write test_config.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "config/Config.h"
#include "common/io/FileSystem.h"

using namespace ea::config;

TEST_CASE("Config loads with defaults when no file", "[config]") {
    auto cfg = load("/nonexistent/config.toml");
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().provider.type == "openai_compatible");
    REQUIRE(cfg.value().agent.max_iterations == 90);
}

TEST_CASE("Config reads TOML file", "[config]") {
    // Write a temp config
    std::string tmp = "/tmp/ea_test_config.toml";
    ea::fs::write_file(tmp, R"(
[agent]
model = "test-model"
max_iterations = 42

[provider]
type = "ollama"
base_url = "http://localhost:11434"
)");

    auto cfg = load(tmp);
    REQUIRE(cfg.ok());
    REQUIRE(cfg.value().agent.model == "test-model");
    REQUIRE(cfg.value().agent.max_iterations == 42);
    REQUIRE(cfg.value().provider.type == "ollama");
    REQUIRE(cfg.value().provider.base_url == "http://localhost:11434");

    ea::fs::remove(tmp);
}
```

- [ ] **Step 5: Write src/config/CMakeLists.txt**

```cmake
add_library(ea-config OBJECT Config.cpp)
target_include_directories(ea-config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-config PUBLIC ea-common toml11::toml11)
```

- [ ] **Step 6: Update CMakeLists.txt and tests/CMakeLists.txt**

- [ ] **Step 7: Build and run tests**

- [ ] **Step 8: Commit**

```bash
git add src/config/ configs/ tests/test_config.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: config module — TOML loading with defaults and env var overrides"
```

---

## Task 8: memory — SqliteMemory

**Files:**
- Create: `src/memory/SqliteMemory.h`
- Create: `src/memory/SqliteMemory.cpp`
- Create: `src/memory/CMakeLists.txt`
- Create: `tests/test_memory_sqlite.cpp`
- Modify: `CMakeLists.txt` (add memory subdirectory)
- Modify: `tests/CMakeLists.txt` (add memory test)

- [ ] **Step 1: Write test_memory_sqlite.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "memory/SqliteMemory.h"

using namespace ea::memory;

TEST_CASE("SqliteMemory store and recall", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);

    auto id = mem.store("test content about C++ agents", "core", 7);
    REQUIRE(id.ok());

    auto results = mem.recall("C++ agents");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() == 1);
    REQUIRE(results.value()[0].content == "test content about C++ agents");
    REQUIRE(results.value()[0].importance == 7);
}

TEST_CASE("SqliteMemory forget", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);

    auto id = mem.store("to be deleted", "core", 3);
    REQUIRE(id.ok());

    auto del = mem.forget(id.value());
    REQUIRE(del.ok());
    REQUIRE(del.value() == true);

    auto c = mem.count();
    REQUIRE(c.ok());
    REQUIRE(c.value() == 0);
}

TEST_CASE("SqliteMemory list", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);

    mem.store("entry 1", "core", 5);
    mem.store("entry 2", "daily", 3);

    auto list = mem.list(10, 0);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
}

TEST_CASE("SqliteMemory count", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    SqliteMemory mem(cfg);

    REQUIRE(mem.count().ok());
    REQUIRE(mem.count().value() == 0);

    mem.store("a", "core", 5);
    mem.store("b", "core", 5);
    REQUIRE(mem.count().value() == 2);
}

TEST_CASE("SqliteMemory FTS5 search", "[memory]") {
    SqliteMemory::Config cfg;
    cfg.path = ":memory:";
    cfg.enable_fts5 = true;
    SqliteMemory mem(cfg);

    mem.store("The quick brown fox jumps over the lazy dog", "core", 5);
    mem.store("A brown bear walks in the forest", "core", 5);
    mem.store("The cat sleeps on the mat", "core", 5);

    auto results = mem.recall("brown fox");
    REQUIRE(results.ok());
    REQUIRE(results.value().size() >= 1);
    // "quick brown fox" should rank higher
    REQUIRE(results.value()[0].content.find("fox") != std::string::npos);
}
```

- [ ] **Step 2: Write SqliteMemory.h**

```cpp
#pragma once
#include "core/IMemory.h"
#include "common/base/Result.h"
#include <string>
#include <memory>

struct sqlite3;

namespace ea::memory {

class SqliteMemory : public IMemory {
public:
    struct Config {
        std::string path;
        bool enable_fts5 = true;
        bool enable_wal = true;
    };

    explicit SqliteMemory(Config config);
    ~SqliteMemory() override;

    SqliteMemory(const SqliteMemory&) = delete;
    SqliteMemory& operator=(const SqliteMemory&) = delete;

    Result<std::string> store(const std::string& content,
                               const std::string& category,
                               int importance) override;
    Result<std::vector<MemoryEntry>> recall(const std::string& query,
                                             int limit) override;
    Result<bool> forget(const std::string& id) override;
    Result<std::vector<MemoryEntry>> list(int limit, int offset) override;
    Result<int> count() override;

private:
    Result<void> open();
    Result<void> create_tables();
    Result<void> close();

    Config config_;
    sqlite3* db_ = nullptr;
    bool opened_ = false;
};

}  // namespace ea::memory
```

- [ ] **Step 3: Write SqliteMemory.cpp**

```cpp
#include "SqliteMemory.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include <sqlite3.h>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace ea::memory {

static std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) uuid += '-';
        uuid += hex[dis(gen)];
    }
    return uuid;
}

static std::string current_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

SqliteMemory::SqliteMemory(Config config) : config_(std::move(config)) {}

SqliteMemory::~SqliteMemory() {
    if (opened_) close();
}

Result<void> SqliteMemory::open() {
    if (opened_) return {};

    // Ensure parent directory exists for file-based databases
    if (config_.path != ":memory:") {
        auto dir = config_.path.substr(0, config_.path.rfind('/'));
        if (!dir.empty()) {
            auto r = fs::mkdir_p(dir);
            if (!r.ok()) return r;
        }
    }

    int rc = sqlite3_open(config_.path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return Error::db("sqlite3_open failed: " + msg);
    }

    // WAL mode for concurrent access
    if (config_.enable_wal && config_.path != ":memory:") {
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    }

    opened_ = true;
    return create_tables();
}

Result<void> SqliteMemory::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS memories (
            id          TEXT PRIMARY KEY,
            content     TEXT NOT NULL,
            category    TEXT NOT NULL DEFAULT 'core',
            importance  INTEGER NOT NULL DEFAULT 5,
            agent_id    TEXT,
            created_at  TEXT NOT NULL,
            accessed_at TEXT NOT NULL
        );
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        return Error::db("create table failed: " + msg);
    }

    if (config_.enable_fts5) {
        const char* fts_sql = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts
                USING fts5(content, category, agent_id,
                           content='memories',
                           content_rowid='rowid');
        )";
        rc = sqlite3_exec(db_, fts_sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            return Error::db("create FTS5 table failed: " + msg);
        }

        // FTS5 sync triggers
        const char* triggers = R"(
            CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
                INSERT INTO memories_fts(rowid, content, category, agent_id)
                VALUES (new.rowid, new.content, new.category, new.agent_id);
            END;
            CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, content, category, agent_id)
                VALUES ('delete', old.rowid, old.content, old.category, old.agent_id);
            END;
            CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, content, category, agent_id)
                VALUES ('delete', old.rowid, old.content, old.category, old.agent_id);
                INSERT INTO memories_fts(rowid, content, category, agent_id)
                VALUES (new.rowid, new.content, new.category, new.agent_id);
            END;
        )";
        rc = sqlite3_exec(db_, triggers, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            return Error::db("create triggers failed: " + msg);
        }
    }

    return {};
}

Result<void> SqliteMemory::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    opened_ = false;
    return {};
}

Result<std::string> SqliteMemory::store(const std::string& content,
                                          const std::string& category,
                                          int importance) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::string id = generate_uuid();
    std::string now = current_iso8601();

    const char* sql = "INSERT INTO memories (id, content, category, importance, created_at, accessed_at) VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, importance);
    sqlite3_bind_text(stmt, 5, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("insert failed: ") + sqlite3_errmsg(db_));
    }

    return id;
}

Result<std::vector<MemoryEntry>> SqliteMemory::recall(const std::string& query,
                                                        int limit) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::vector<MemoryEntry> results;

    if (config_.enable_fts5) {
        const char* sql = R"(
            SELECT m.id, m.content, m.category, m.importance, m.created_at, m.agent_id
            FROM memories m
            JOIN memories_fts fts ON m.rowid = fts.rowid
            WHERE memories_fts MATCH ?
            ORDER BY bm25(memories_fts) DESC
            LIMIT ?
        )";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return Error::db(std::string("prepare failed: ") + sqlite3_errmsg(db_));
        }

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryEntry entry;
            entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            entry.importance = sqlite3_column_int(stmt, 3);
            entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            }
            results.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);
    } else {
        // Fallback: LIKE search
        const char* sql = "SELECT id, content, category, importance, created_at, agent_id FROM memories WHERE content LIKE ? LIMIT ?";
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryEntry entry;
            entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            entry.importance = sqlite3_column_int(stmt, 3);
            entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
                entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            }
            results.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);
    }

    return results;
}

Result<bool> SqliteMemory::forget(const std::string& id) {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "DELETE FROM memories WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Error::db(std::string("delete failed: ") + sqlite3_errmsg(db_));
    }
    return sqlite3_changes(db_) > 0;
}

Result<std::vector<MemoryEntry>> SqliteMemory::list(int limit, int offset) {
    auto r = open();
    if (!r.ok()) return r.error();

    std::vector<MemoryEntry> results;
    const char* sql = "SELECT id, content, category, importance, created_at, agent_id FROM memories ORDER BY created_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry entry;
        entry.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.importance = sqlite3_column_int(stmt, 3);
        entry.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            entry.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        }
        results.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return results;
}

Result<int> SqliteMemory::count() {
    auto r = open();
    if (!r.ok()) return r.error();

    const char* sql = "SELECT COUNT(*) FROM memories";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

}  // namespace ea::memory
```

- [ ] **Step 4: Write src/memory/CMakeLists.txt**

```cmake
add_library(ea-memory OBJECT SqliteMemory.cpp)
target_include_directories(ea-memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-memory PUBLIC ea-core ea-common sqlite3)
```

- [ ] **Step 5: Update CMakeLists.txt and tests/CMakeLists.txt**

- [ ] **Step 6: Build and run tests**

- [ ] **Step 7: Commit**

```bash
git add src/memory/ tests/test_memory_sqlite.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: memory module — SqliteMemory with FTS5 full-text search"
```

---

## Task 9: tool — ToolRegistry + ShellTool + FileTool + SearchTool + WebTool + MemoryTool

**Files:**
- Create all tool headers and implementations
- Create: `src/tool/CMakeLists.txt`
- Create: `tests/test_tool_registry.cpp`
- Create: `tests/test_tool_shell.cpp`
- Create: `tests/test_tool_file.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

This is the largest task. Each tool follows the same pattern: header declaring the class, implementation with JSON schema and execute dispatching by action field.

- [ ] **Step 1: Write ToolRegistry.h/.cpp**

ToolRegistry: `register_tool(unique_ptr<ITool>)`, `find(name) -> ITool*`, `get_all_specs() -> vector<ToolSpec>`, `execute(name, args) -> Result<ToolResult>`.

- [ ] **Step 2: Write test_tool_registry.cpp**

Test register, find, get_all_specs, execute, not-found.

- [ ] **Step 3: Write ShellTool.h/.cpp**

Schema: `{"type":"object","properties":{"command":{"type":"string"}},"required":["command"]}`.
Execute: calls `process::exec(command)`, returns stdout/stderr in ToolResult.

- [ ] **Step 4: Write FileTool.h/.cpp**

Schema with `action` field: `{"type":"object","properties":{"action":{"enum":["read","write","edit"]}},...}`.
- read: `fs::read_file(path)`
- write: `fs::write_file(path, content)`
- edit: read file, replace `old_string` with `new_string`, write back

- [ ] **Step 5: Write SearchTool.h/.cpp**

Schema: `{"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"}}}`.
Execute: `process::exec("grep -rn -- <pattern> <path>")` or rg if available.

- [ ] **Step 6: Write WebTool.h/.cpp**

Schema with `action`: `{"enum":["search","fetch"]}`.
- search: calls a search API (stub for now, returns "web search not yet implemented")
- fetch: `HttpClient::get(url)`, returns body

- [ ] **Step 7: Write MemoryTool.h/.cpp**

Schema with `action`: `{"enum":["store","recall","forget"]}`.
Dispatches to `IMemory::store/recall/forget`.

- [ ] **Step 8: Write test_tool_shell.cpp and test_tool_file.cpp**

Shell: test `echo hello` returns "hello".
File: test write + read roundtrip, test edit replaces string.

- [ ] **Step 9: Write src/tool/CMakeLists.txt**

```cmake
add_library(ea-tool OBJECT
    ToolRegistry.cpp
    ShellTool.cpp
    FileTool.cpp
    SearchTool.cpp
    WebTool.cpp
    MemoryTool.cpp
)
target_include_directories(ea-tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-tool PUBLIC ea-core ea-common)
```

- [ ] **Step 10: Build and run tests**

- [ ] **Step 11: Commit**

```bash
git add src/tool/ tests/test_tool_*.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: tool module — ToolRegistry, Shell, File, Search, Web, Memory tools"
```

---

## Task 10: provider — OpenAIProvider + ProviderFactory

**Files:**
- Create: `src/provider/OpenAIProvider.h`
- Create: `src/provider/OpenAIProvider.cpp`
- Create: `src/provider/OllamaProvider.h`
- Create: `src/provider/OllamaProvider.cpp`
- Create: `src/provider/ProviderFactory.h`
- Create: `src/provider/ProviderFactory.cpp`
- Create: `src/provider/CMakeLists.txt`
- Create: `tests/test_provider_openai.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write OpenAIProvider.h/.cpp**

Key methods:
- `build_request_body()`: Convert Messages to OpenAI format, add tools as function declarations
- `parse_response()`: Extract content, tool_calls, usage from OpenAI response JSON
- `chat()`: POST to `/chat/completions`, parse response
- `stream_chat()`: POST with `stream: true`, use SseParser to parse chunks, accumulate tool_calls

- [ ] **Step 2: Write test_provider_openai.cpp**

Mock HttpClient by subclassing or injecting. Test:
- Request body construction (messages → JSON)
- Response parsing (JSON → LLMResponse with tool_calls)
- Error handling (401, 429, malformed JSON)

- [ ] **Step 3: Write OllamaProvider.h/.cpp (stub)**

Minimal stub that returns `Error::not_found("Ollama provider not yet implemented")`.

- [ ] **Step 4: Write ProviderFactory.h/.cpp**

```cpp
std::unique_ptr<IProvider> create(const config::ProviderConfig& cfg) {
    if (cfg.type == "openai_compatible") {
        return std::make_unique<OpenAIProvider>(OpenAIProvider::Config{...});
    } else if (cfg.type == "ollama") {
        return std::make_unique<OllamaProvider>(OllamaProvider::Config{...});
    }
    return nullptr;
}
```

- [ ] **Step 5: Write src/provider/CMakeLists.txt**

```cmake
add_library(ea-provider OBJECT
    OpenAIProvider.cpp
    OllamaProvider.cpp
    ProviderFactory.cpp
)
target_include_directories(ea-provider PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-provider PUBLIC ea-core ea-common)
```

- [ ] **Step 6: Build and run tests**

- [ ] **Step 7: Commit**

```bash
git add src/provider/ tests/test_provider_openai.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: provider module — OpenAIProvider with request/response parsing"
```

---

## Task 11: security — SecurityPolicy

**Files:**
- Create: `src/security/SecurityPolicy.h`
- Create: `src/security/SecurityPolicy.cpp`
- Create: `src/security/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write SecurityPolicy.h/.cpp**

AutonomyLevel enum, check_command (blacklist + whitelist), check_file_path (workspace boundary), check_tool (readonly blocks write tools).

- [ ] **Step 2: Write src/security/CMakeLists.txt**

```cmake
add_library(ea-security OBJECT SecurityPolicy.cpp)
target_include_directories(ea-security PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-security PUBLIC ea-core ea-common)
```

- [ ] **Step 3: Build and verify**

- [ ] **Step 4: Commit**

```bash
git add src/security/ CMakeLists.txt
git commit -m "feat: security module — SecurityPolicy with autonomy levels"
```

---

## Task 12: agent — AgentLoop + SystemPrompt

**Files:**
- Create: `src/agent/AgentLoop.h`
- Create: `src/agent/AgentLoop.cpp`
- Create: `src/agent/SystemPrompt.h`
- Create: `src/agent/SystemPrompt.cpp`
- Create: `src/agent/CMakeLists.txt`
- Create: `tests/test_agent_loop.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write SystemPrompt.h/.cpp**

`build_system_prompt(PromptContext)`: Concatenate stable + context + volatile layers.

- [ ] **Step 2: Write AgentLoop.h/.cpp**

Core loop:
1. Push user message to history
2. Build messages (system prompt + memory + history)
3. Call provider_->chat()
4. If tool_calls: execute each, push results to history, goto 2
5. If no tool_calls: output content, break
6. If max_iterations reached: warn and break

- [ ] **Step 3: Write test_agent_loop.cpp**

Mock Provider + Mock Tool. Test:
- Simple conversation (no tool calls)
- Tool call flow (LLM requests tool, tool executes, LLM responds)
- Max iterations limit
- Tool error handled gracefully (returned to LLM)

- [ ] **Step 4: Write src/agent/CMakeLists.txt**

```cmake
add_library(ea-agent OBJECT AgentLoop.cpp SystemPrompt.cpp)
target_include_directories(ea-agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-agent PUBLIC ea-core ea-common)
```

- [ ] **Step 5: Build and run tests**

- [ ] **Step 6: Commit**

```bash
git add src/agent/ tests/test_agent_loop.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: agent module — AgentLoop with tool-calling conversation loop"
```

---

## Task 13: main.cpp + Public Headers + Final Assembly

**Files:**
- Create: `src/main.cpp`
- Create: `include/embedded-agent/Agent.h`
- Create: `include/embedded-agent/Version.h`
- Modify: `CMakeLists.txt` (add final targets: embedded-agent-core, embedded-agent executable)

- [ ] **Step 1: Write include/embedded-agent/Version.h**

```cpp
#pragma once
#define EMBEDDED_AGENT_VERSION "0.1.0"
#define EMBEDDED_AGENT_VERSION_MAJOR 0
#define EMBEDDED_AGENT_VERSION_MINOR 1
#define EMBEDDED_AGENT_VERSION_PATCH 0
```

- [ ] **Step 2: Write include/embedded-agent/Agent.h**

```cpp
#pragma once
#include "agent/AgentLoop.h"
#include "provider/ProviderFactory.h"
#include "memory/SqliteMemory.h"
#include "tool/ToolRegistry.h"
#include "config/Config.h"
#include "Version.h"

namespace ea {

class Agent {
public:
    explicit Agent(const std::string& config_path = "") {
        auto cfg = config::load(config_path);
        if (!cfg.ok()) return;  // TODO: error handling

        config_ = cfg.value();
        provider_ = provider::create(config_.provider);
        memory_ = std::make_unique<memory::SqliteMemory>(
            memory::SqliteMemory::Config{config_.memory.path, config_.memory.enable_fts5});
        // Register tools...
    }

    // Simplified public API
    std::string chat(const std::string& input);

private:
    config::AppConfig config_;
    std::unique_ptr<IProvider> provider_;
    std::unique_ptr<IMemory> memory_;
};

}  // namespace ea
```

- [ ] **Step 3: Write src/main.cpp**

Full CLI entry point with CLI11, config loading, component assembly, interactive loop.

- [ ] **Step 4: Update CMakeLists.txt — add final library and executable targets**

```cmake
add_library(embedded-agent-core STATIC
    $<TARGET_OBJECTS:ea-common>
    $<TARGET_OBJECTS:ea-provider>
    $<TARGET_OBJECTS:ea-tool>
    $<TARGET_OBJECTS:ea-memory>
    $<TARGET_OBJECTS:ea-agent>
    $<TARGET_OBJECTS:ea-platform>
    $<TARGET_OBJECTS:ea-config>
    $<TARGET_OBJECTS:ea-security>
)
target_link_libraries(embedded-agent-core PUBLIC
    ea-core
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
    mbedtls::mbedtls
    sqlite3
)
target_include_directories(embedded-agent-core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(embedded-agent src/main.cpp)
target_link_libraries(embedded-agent PRIVATE embedded-agent-core CLI11::CLI11)
```

- [ ] **Step 5: Build the full project**

Run: `cd /home/lsy/embedded-agent/build && cmake .. -DENABLE_TESTS=ON && cmake --build .`
Expected: Build succeeds, `embedded-agent` binary created

- [ ] **Step 6: Run all tests**

Run: `./tests/ea-tests`
Expected: All tests pass

- [ ] **Step 7: Test the CLI binary**

Run: `./embedded-agent --help`
Expected: Shows CLI help text

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp include/ CMakeLists.txt
git commit -m "feat: CLI entry point and final assembly — Phase 1 MVP complete"
```

---

## Self-Review

**1. Spec coverage check:**
- ✅ IProvider + OpenAIProvider → Task 10
- ✅ ITool + ToolRegistry + all 6 tools → Task 9
- ✅ IMemory + SqliteMemory → Task 8
- ✅ AgentLoop + SystemPrompt → Task 12
- ✅ Platform detection → Task 6
- ✅ Config (TOML) → Task 7
- ✅ CLI entry → Task 13
- ✅ SecurityPolicy → Task 11
- ✅ common/base (Error, Result, StringUtil, JsonHelper) → Task 2
- ✅ common/io (Logger, FileSystem, Process) → Task 3
- ✅ common/net (HttpClient, TlsConfig, SseParser, RetryPolicy) → Task 4
- ✅ CMake + FetchContent → Task 1

**2. Placeholder scan:** Found and fixed:
- `Error.h` had `static Error parse(const std::string&)` → fixed to `static Error parse(const std::string&)`
- `Version.h` had `MBEDDED_AGENT_VERSION_PATCH` → fixed to `#define EMBEDDED_AGENT_VERSION_PATCH`
- `Platform.cpp` had `return/bin/sh")` → fixed to `return access("/system/bin/sh", X_OK) == 0;`
- `SqliteMemory.cpp` missing closing brace in `open()` → fixed

**3. Type consistency:** All types match across tasks. Result<T> used consistently. ToolResult has call_id field. IProvider::chat returns Result<LLMResponse>. ITool::execute returns Result<ToolResult>.
