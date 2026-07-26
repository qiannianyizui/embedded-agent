# Task 7 Report: Ensure data_dir exists before creating databases

## What was implemented

Added a `mkdir_p(data_dir())` call at the beginning of `AppBuilder::build()` in `src/app/AppBuilder.cpp`, immediately after `ctx.debug = debug;`. This ensures the data directory (now at `config_dir()/data`) is created before any database files are opened.

The implementation:
- Calls `fs::data_dir()` to resolve the data directory path
- If resolution succeeds, calls `fs::mkdir_p()` to create the directory (and parents) if they don't exist
- If directory creation fails, logs a warning via `EA_WARN` but does not abort — the app continues, and individual components (memory, usage store, conversation store) will handle their own open failures

No new includes were needed since `"common/io/FileSystem.h"` and `"common/io/Logger.h"` were already present.

## Test results

All test suites passed:

| Suite | Assertions | Test Cases | Result |
|-------|-----------|------------|--------|
| Unit  | 1292      | 452        | PASS   |
| Integration | 83   | 25         | PASS   |
| System | 99       | 35         | PASS   |
| Security | 173     | 42         | PASS   |

## Concerns or issues

None. The change is a defensive mkdir that is a no-op when the directory already exists. Existing per-component mkdir logic (e.g., in the memory path resolution) remains in place as redundant safety.
