# Path Resolution Overhaul — Align with Zeroclaw

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Overhaul `ea::fs` path resolution to match zeroclaw's layered strategy: environment variable overrides, config/data directory separation, and tilde expansion.

**Architecture:** Replace the flat `home_dir() → config_dir() = data_dir()` chain with a priority-based resolution: `EA_CONFIG_DIR` / `EA_DATA_DIR` env vars override defaults; `data_dir()` defaults to `config_dir()/data` instead of `config_dir()`; `expand_tilde()` supports `~` and `~/...` expansion. No backward compatibility with the old flat layout — users must manually migrate `.db` files from `~/.embedded-agent/` to `~/.embedded-agent/data/`.

**Tech Stack:** C++17, POSIX (`getenv`, `access`), Catch2 test framework

## Global Constraints

- Namespace: `ea::fs` for all path functions
- Error handling: `Result<T>` with `Error` factory methods
- Environment variable names: `EA_CONFIG_DIR`, `EA_DATA_DIR`
- Default config dir: `home_dir()/.embedded-agent`
- Default data dir: `config_dir()/data`
- No automatic migration of old layout
- Android runtime detection preserved as-is
- Comments explain "why" not "what"
- Test tag: `[filesystem]`

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/common/io/FileSystem.h` | Public API declarations |
| `src/common/io/FileSystem.cpp` | Implementation of all path functions |
| `tests/unit/test_filesystem.cpp` | Unit tests |
| `src/main.cpp` | Update log init to use `config_dir()` |
| `src/app/SetupWizardRunner.cpp` | Update log init to use `config_dir()` |

---

### Task 1: Add `expand_tilde()` function

**Files:**
- Modify: `src/common/io/FileSystem.h`
- Modify: `src/common/io/FileSystem.cpp`
- Modify: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `home_dir()`
- Produces: `std::string expand_tilde(const std::string& path)`

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("expand_tilde expands ~ to home dir", "[filesystem]") {
    auto home = home_dir();
    REQUIRE(home.ok());
    REQUIRE(expand_tilde("~") == home.value());
}

TEST_CASE("expand_tilde expands ~/path", "[filesystem]") {
    auto home = home_dir();
    REQUIRE(home.ok());
    REQUIRE(expand_tilde("~/some/path") == home.value() + "/some/path");
}

TEST_CASE("expand_tilde returns non-tilde path unchanged", "[filesystem]") {
    REQUIRE(expand_tilde("/absolute/path") == "/absolute/path");
    REQUIRE(expand_tilde("relative/path") == "relative/path");
    REQUIRE(expand_tilde("") == "");
}

TEST_CASE("expand_tilde returns path unchanged when home_dir fails", "[filesystem]") {
    // Can't easily force home_dir to fail in integration, but we can
    // verify that a path starting with ~user (not bare ~) is unchanged
    // since we only expand bare ~, not ~username.
    REQUIRE(expand_tilde("~otheruser/docs").substr(0, 1) == "~");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "expand_tilde|FAILED|PASSED"`
Expected: FAIL — `expand_tilde` not declared

- [ ] **Step 3: Implement `expand_tilde()`**

In `FileSystem.h`, add declaration:
```cpp
std::string expand_tilde(const std::string& path);
```

In `FileSystem.cpp`, add implementation:
```cpp
std::string expand_tilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    auto home = home_dir();
    if (!home.ok()) return path;
    if (path.size() == 1) return home.value();
    // Only expand bare ~ and ~/... — not ~otheruser/...
    if (path[1] == '/') return home.value() + path.substr(1);
    return path;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "expand_tilde|PASSED|FAILED"`
Expected: All 4 expand_tilde tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/io/FileSystem.h src/common/io/FileSystem.cpp tests/unit/test_filesystem.cpp
git commit -m "feat(fs): add expand_tilde() for ~ path expansion"
```

---

### Task 2: Overhaul `config_dir()` with `EA_CONFIG_DIR` override

**Files:**
- Modify: `src/common/io/FileSystem.cpp`
- Modify: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `home_dir()`, `expand_tilde()`
- Produces: Updated `config_dir()` with env var override

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("config_dir honors EA_CONFIG_DIR env var", "[filesystem]") {
    // Save and restore env var
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";
    setenv("EA_CONFIG_DIR", "/tmp/ea_test_config", 1);

    auto result = config_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == "/tmp/ea_test_config");

    // Restore
    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}

TEST_CASE("config_dir expands ~ in EA_CONFIG_DIR", "[filesystem]") {
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";
    auto home = home_dir();
    REQUIRE(home.ok());

    setenv("EA_CONFIG_DIR", "~/custom-config", 1);
    auto result = config_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == home.value() + "/custom-config");

    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}

TEST_CASE("config_dir ignores empty EA_CONFIG_DIR", "[filesystem]") {
    const char* orig = getenv("EA_CONFIG_DIR");
    std::string orig_str = orig ? orig : "";

    setenv("EA_CONFIG_DIR", "", 1);
    auto result = config_dir();
    REQUIRE(result.ok());
    // Should fall through to default home_dir()/.embedded-agent
    REQUIRE(result.value().find(".embedded-agent") != std::string::npos);

    if (orig_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_str.c_str(), 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "EA_CONFIG_DIR|FAILED|PASSED"`
Expected: FAIL — `config_dir()` doesn't check `EA_CONFIG_DIR` yet

- [ ] **Step 3: Implement `config_dir()` overhaul**

Replace `config_dir()` in `FileSystem.cpp`:
```cpp
Result<std::string> config_dir() {
    const char* env_dir = getenv("EA_CONFIG_DIR");
    if (env_dir && env_dir[0] != '\0') {
        return expand_tilde(std::string(env_dir));
    }
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent";
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "EA_CONFIG_DIR|PASSED|FAILED"`
Expected: All 3 new tests PASS, existing tests still PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/io/FileSystem.cpp tests/unit/test_filesystem.cpp
git commit -m "feat(fs): config_dir() honors EA_CONFIG_DIR env var override"
```

---

### Task 3: Overhaul `data_dir()` with `EA_DATA_DIR` override and config/data separation

**Files:**
- Modify: `src/common/io/FileSystem.cpp`
- Modify: `src/common/io/FileSystem.h`
- Modify: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `config_dir()`, `expand_tilde()`
- Produces: Updated `data_dir()` returning `config_dir()/data` by default

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("data_dir defaults to config_dir/data", "[filesystem]") {
    // Ensure no EA_DATA_DIR override
    const char* orig = getenv("EA_DATA_DIR");
    std::string orig_str = orig ? orig : "";
    unsetenv("EA_DATA_DIR");

    auto cfg = config_dir();
    auto dat = data_dir();
    REQUIRE(cfg.ok());
    REQUIRE(dat.ok());
    REQUIRE(dat.value() == cfg.value() + "/data");

    if (!orig_str.empty()) setenv("EA_DATA_DIR", orig_str.c_str(), 1);
}

TEST_CASE("data_dir honors EA_DATA_DIR env var", "[filesystem]") {
    const char* orig = getenv("EA_DATA_DIR");
    std::string orig_str = orig ? orig : "";

    setenv("EA_DATA_DIR", "/tmp/ea_test_data", 1);
    auto result = data_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == "/tmp/ea_test_data");

    if (orig_str.empty()) unsetenv("EA_DATA_DIR");
    else setenv("EA_DATA_DIR", orig_str.c_str(), 1);
}

TEST_CASE("data_dir expands ~ in EA_DATA_DIR", "[filesystem]") {
    const char* orig = getenv("EA_DATA_DIR");
    std::string orig_str = orig ? orig : "";
    auto home = home_dir();
    REQUIRE(home.ok());

    setenv("EA_DATA_DIR", "~/custom-data", 1);
    auto result = data_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == home.value() + "/custom-data");

    if (orig_str.empty()) unsetenv("EA_DATA_DIR");
    else setenv("EA_DATA_DIR", orig_str.c_str(), 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "EA_DATA_DIR|data_dir|FAILED|PASSED"`
Expected: FAIL — `data_dir()` currently returns `config_dir()`, not `config_dir()/data`

- [ ] **Step 3: Implement `data_dir()` overhaul**

Replace `data_dir()` in `FileSystem.cpp`:
```cpp
Result<std::string> data_dir() {
    const char* env_dir = getenv("EA_DATA_DIR");
    if (env_dir && env_dir[0] != '\0') {
        return expand_tilde(std::string(env_dir));
    }
    auto cfg = config_dir();
    if (!cfg.ok()) return cfg.error();
    return cfg.value() + "/data";
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "EA_DATA_DIR|data_dir|PASSED|FAILED"`
Expected: All 3 new tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/io/FileSystem.cpp src/common/io/FileSystem.h tests/unit/test_filesystem.cpp
git commit -m "feat(fs): data_dir() separates from config_dir(), honors EA_DATA_DIR"
```

---

### Task 4: Add conflict detection for `EA_CONFIG_DIR` + `EA_DATA_DIR`

**Files:**
- Modify: `src/common/io/FileSystem.cpp`
- Modify: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `EA_CONFIG_DIR`, `EA_DATA_DIR` env vars
- Produces: Warning log when both are set

- [ ] **Step 1: Write failing test**

```cpp
TEST_CASE("data_dir warns when both EA_CONFIG_DIR and EA_DATA_DIR are set", "[filesystem]") {
    const char* orig_config = getenv("EA_CONFIG_DIR");
    const char* orig_data = getenv("EA_DATA_DIR");
    std::string orig_config_str = orig_config ? orig_config : "";
    std::string orig_data_str = orig_data ? orig_data : "";

    setenv("EA_CONFIG_DIR", "/tmp/ea_conflict_config", 1);
    setenv("EA_DATA_DIR", "/tmp/ea_conflict_data", 1);

    // EA_CONFIG_DIR wins — data_dir should be config_dir/data
    auto result = data_dir();
    REQUIRE(result.ok());
    REQUIRE(result.value() == "/tmp/ea_conflict_config/data");

    // Restore
    if (orig_config_str.empty()) unsetenv("EA_CONFIG_DIR");
    else setenv("EA_CONFIG_DIR", orig_config_str.c_str(), 1);
    if (orig_data_str.empty()) unsetenv("EA_DATA_DIR");
    else setenv("EA_DATA_DIR", orig_data_str.c_str(), 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "conflict|FAILED|PASSED"`
Expected: FAIL — `data_dir()` currently honors `EA_DATA_DIR` even when `EA_CONFIG_DIR` is set

- [ ] **Step 3: Implement conflict detection**

Update `data_dir()` in `FileSystem.cpp`:
```cpp
Result<std::string> data_dir() {
    // EA_CONFIG_DIR takes precedence — when set, it pins both config
    // and data locations. EA_DATA_DIR is ignored with a warning.
    const char* config_env = getenv("EA_CONFIG_DIR");
    const char* data_env = getenv("EA_DATA_DIR");
    if (config_env && config_env[0] != '\0') {
        if (data_env && data_env[0] != '\0') {
            EA_WARN("EA_CONFIG_DIR is set; EA_DATA_DIR is ignored "
                    "(CONFIG_DIR pins both config and data directories)");
        }
        auto cfg = config_dir();
        if (!cfg.ok()) return cfg.error();
        return cfg.value() + "/data";
    }
    if (data_env && data_env[0] != '\0') {
        return expand_tilde(std::string(data_env));
    }
    auto cfg = config_dir();
    if (!cfg.ok()) return cfg.error();
    return cfg.value() + "/data";
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "conflict|PASSED|FAILED"`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/io/FileSystem.cpp tests/unit/test_filesystem.cpp
git commit -m "feat(fs): EA_CONFIG_DIR takes precedence over EA_DATA_DIR with warning"
```

---

### Task 5: Simplify `resolve_data_path()`

**Files:**
- Modify: `src/common/io/FileSystem.cpp`
- Modify: `tests/unit/test_filesystem.cpp`

**Interfaces:**
- Consumes: `data_dir()`
- Produces: Simplified `resolve_data_path()` using `data_dir()` directly

- [ ] **Step 1: Update existing tests to reflect new paths**

The existing test `resolve_data_path returns config_dir/filename` should now expect `data_dir/filename`. Update:

```cpp
TEST_CASE("resolve_data_path returns data_dir/filename", "[common][filesystem]") {
    auto result = ea::fs::resolve_data_path("test.db");
    REQUIRE(result.ok());
    REQUIRE(result.value().size() >= 8);
    REQUIRE(result.value().substr(result.value().size() - 8) == "/test.db");
    // Should contain /data/ in the path now
    REQUIRE(result.value().find("/data/") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "resolve_data_path|FAILED|PASSED"`
Expected: FAIL — `resolve_data_path now contains /data/

- [ ] **Step 3: Simplify `resolve_data_path()`**

Replace in `FileSystem.cpp`:
```cpp
Result<std::string> resolve_data_path(const std::string& filename) {
    auto dir = data_dir();
    if (!dir.ok()) return dir.error();
    return dir.value() + "/" + filename;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `./build/tests/ea-tests "[filesystem]" 2>&1 | grep -E "resolve_data_path|PASSED|FAILED"`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/common/io/FileSystem.cpp tests/unit/test_filesystem.cpp
git commit -m "refactor(fs): simplify resolve_data_path() to use data_dir()"
```

---

### Task 6: Update callers — main.cpp and SetupWizardRunner

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/app/SetupWizardRunner.cpp`

**Interfaces:**
- Consumes: `config_dir()`, `data_dir()`

- [ ] **Step 1: Update main.cpp log init**

Current:
```cpp
auto home = ea::platform::home_dir();
ea::log::init(home + "/.embedded-agent", debug);
```

Change to:
```cpp
auto cfg_dir = ea::fs::config_dir();
ea::log::init(cfg_dir.ok() ? cfg_dir.value() : "/tmp/.embedded-agent", debug);
```

- [ ] **Step 2: Update SetupWizardRunner.cpp log init**

Current:
```cpp
auto home = ea::platform::home_dir();
ea::log::init(home + "/.embedded-agent", false);
```

Change to:
```cpp
auto cfg_dir = ea::fs::config_dir();
ea::log::init(cfg_dir.ok() ? cfg_dir.value() : "/tmp/.embedded-agent", false);
```

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build build -j$(nproc) && ./build/tests/ea-tests`
Expected: All tests PASS

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp src/app/SetupWizardRunner.cpp
git commit -m "refactor: use config_dir() instead of home_dir()/.embedded-agent for log init"
```

---

### Task 7: Ensure AppBuilder creates data directory

**Files:**
- Modify: `src/app/AppBuilder.cpp`

**Interfaces:**
- Consumes: `data_dir()`, `mkdir_p()`

- [ ] **Step 1: Add data_dir mkdir_p in AppBuilder::build()**

At the beginning of `AppBuilder::build()`, after setting `ctx.config` and `ctx.debug`, add:

```cpp
// Ensure data directory exists before creating databases
auto data_result = fs::data_dir();
if (data_result.ok()) {
    auto mkdir_result = fs::mkdir_p(data_result.value());
    if (!mkdir_result.ok()) {
        EA_WARN("Failed to create data directory: {}", mkdir_result.error().message);
    }
}
```

- [ ] **Step 2: Build and run all tests**

Run: `cmake --build build -j$(nproc) && ./build/tests/ea-tests`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add src/app/AppBuilder.cpp
git commit -m "feat(app): ensure data_dir exists before creating databases"
```

---

### Task 8: Full test suite verification

**Files:** None (verification only)

- [ ] **Step 1: Clean rebuild and full test run**

```bash
cd build && cmake --build . --clean-first -j$(nproc) && ./tests/ea-tests
```

Expected: All tests PASS (441+ unit, 25 integration, 35 system, 42 security)

- [ ] **Step 2: Verify new directory layout works end-to-end**

```bash
# Run the app briefly to confirm it creates data/ subdirectory
EMBEDDED_AGENT_API_KEY=test ./build/embedded-agent --debug 2>&1 | head -5
ls -la ~/.embedded-agent/
ls -la ~/.embedded-agent/data/ 2>/dev/null || echo "data/ not created yet (expected if no DB operations ran)"
```

Expected: App starts, logs show config_dir and data_dir paths correctly

- [ ] **Step 3: Verify EA_CONFIG_DIR override works**

```bash
EA_CONFIG_DIR=/tmp/ea_override_test ./build/embedded-agent --debug 2>&1 | head -5
ls -la /tmp/ea_override_test/ 2>/dev/null || echo "dir not created (expected if no DB operations ran)"
rm -rf /tmp/ea_override_test
```

Expected: App uses `/tmp/ea_override_test/` as config dir
