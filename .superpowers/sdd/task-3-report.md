# Task 3 Report: data_dir() Separates from config_dir(), Honors EA_DATA_DIR

## Status: DONE

## Files Modified

| File | Action | Description |
|------|--------|-------------|
| `src/common/io/FileSystem.cpp` | Modified | `data_dir()` now checks `EA_DATA_DIR` env var, falls back to `config_dir()/data` |
| `tests/unit/test_filesystem.cpp` | Modified | Added 3 test cases for `data_dir()` behavior |

## What was implemented

Modified `data_dir()` in `src/common/io/FileSystem.cpp` to:

1. Check `EA_DATA_DIR` environment variable first; if set and non-empty, return `expand_tilde()` of its value
2. Fall back to `config_dir() + "/data"` instead of the previous `config_dir()` (which made config=data identical)

This separates the data directory from the config directory, allowing users to store data in a different location from configuration.

## Test results

All 23 filesystem test cases passed (51 assertions), including the 3 new ones:

- `data_dir defaults to config_dir/data` — PASS
- `data_dir honors EA_DATA_DIR env var` — PASS
- `data_dir expands ~ in EA_DATA_DIR` — PASS

## Concerns or issues

None. The implementation follows the same pattern as the existing `config_dir()` with `EA_CONFIG_DIR`, and all tests pass cleanly.
