# Task 4 Report: EA_CONFIG_DIR takes precedence over EA_DATA_DIR

## What was implemented

Modified `data_dir()` in `src/common/io/FileSystem.cpp` to check `EA_CONFIG_DIR` first. When `EA_CONFIG_DIR` is set, `EA_DATA_DIR` is ignored and a warning is logged via `EA_WARN`. This ensures that when a user pins the config directory, the data directory is derived from it (as `config_dir/data`) rather than potentially pointing to a separate location via `EA_DATA_DIR`.

### Changes made

1. **`src/common/io/FileSystem.cpp`**:
   - Added `#include "common/io/Logger.h"` for `EA_WARN` macro
   - Replaced `data_dir()` implementation with conflict detection logic:
     - If `EA_CONFIG_DIR` is set and `EA_DATA_DIR` is also set, log a warning and ignore `EA_DATA_DIR`
     - If only `EA_CONFIG_DIR` is set, derive data dir as `config_dir()/data`
     - If only `EA_DATA_DIR` is set, use it (with tilde expansion)
     - If neither is set, fall back to `config_dir()/data`

2. **`tests/unit/test_filesystem.cpp`**:
   - Added test case `"EA_CONFIG_DIR takes precedence over EA_DATA_DIR"` that sets both env vars and verifies `data_dir()` returns `config_dir()/data` (ignoring `EA_DATA_DIR`)

## Test results

- All 24 filesystem test cases passed (53 assertions)
- The new test case correctly verifies that when both `EA_CONFIG_DIR` and `EA_DATA_DIR` are set, `data_dir()` returns `/tmp/ea_conflict_config/data` (derived from `EA_CONFIG_DIR`), not `/tmp/ea_conflict_data`
- The `EA_WARN` log message was confirmed in test output

## Concerns or issues

None. The implementation matches the spec exactly. The warning message is clear and informative. Environment variable cleanup in the test follows the existing pattern in the test file.
