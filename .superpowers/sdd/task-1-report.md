# Task 1 Report: expand_tilde() for ~ Path Expansion

## Status: DONE

## What Was Implemented

Added `expand_tilde()` function to `ea::fs` namespace in the FileSystem module:

- **Declaration** in `src/common/io/FileSystem.h` — after `data_dir()`
- **Implementation** in `src/common/io/FileSystem.cpp` — before `home_dir()`
- **4 unit tests** in `tests/unit/test_filesystem.cpp`

### Behavior

| Input | Output |
|---|---|
| `""` | `""` (unchanged) |
| `"/absolute/path"` | `"/absolute/path"` (unchanged) |
| `"relative/path"` | `"relative/path"` (unchanged) |
| `"~"` | `$HOME` |
| `"~/some/path"` | `$HOME/some/path` |
| `"~otheruser/docs"` | `"~otheruser/docs"` (unchanged — no user-home expansion) |

If `home_dir()` fails, the original path is returned unchanged (graceful fallback).

## Test Results

All 17 filesystem test cases passed (36 assertions), including the 4 new ones:

- `expand_tilde expands ~ to home dir` — PASS
- `expand_tilde expands ~/path` — PASS
- `expand_tilde returns non-tilde path unchanged` — PASS
- `expand_tilde does not expand ~otheruser` — PASS

## Concerns

None. Implementation matches the spec exactly. The function is a pure utility with no side effects and graceful error handling.
