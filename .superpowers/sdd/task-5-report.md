# Task 5 Report: Simplify resolve_data_path() to use data_dir()

## What was implemented

Replaced `resolve_data_path()` in `src/common/io/FileSystem.cpp` with a simpler implementation that delegates directly to `data_dir()` instead of manually checking `config_dir()` and falling back to `home_dir()/.embedded-agent/`.

**Old implementation:**
- Checked `config_dir()` first, appending `"/" + filename`
- Fell back to `home_dir() + "/.embedded-agent/" + filename`

**New implementation:**
- Calls `data_dir()` and appends `"/" + filename`
- Propagates any error from `data_dir()`

This ensures data files are consistently placed under the `data/` subdirectory (e.g., `~/.embedded-agent/data/test.db` instead of `~/.embedded-agent/test.db`), aligning with the path resolution overhaul where `data_dir()` already handles `EA_CONFIG_DIR`, `EA_DATA_DIR`, and default logic.

**Test update:**
- Renamed test from `"resolve_data_path returns config_dir/filename when config_dir ok"` to `"resolve_data_path returns data_dir/filename"`
- Added assertion: `result.value().find("/data/") != std::string::npos` to verify the path now includes `/data/`

## Test results

- **24 test cases passed**, **54 assertions passed**
- Zero failures
- The EA_CONFIG_DIR/EA_DATA_DIR conflict warning is expected (from the precedence test case)

## Concerns or issues

None. The change is a straightforward delegation to `data_dir()`, which already contains all the necessary logic (env var handling, tilde expansion, conflict detection) from Tasks 1-4.
