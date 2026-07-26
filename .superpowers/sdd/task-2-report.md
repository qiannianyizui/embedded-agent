# Task 2 Report: config_dir() honors EA_CONFIG_DIR env var override

## Status: DONE

## Files Modified

| File | Action | Description |
|------|--------|-------------|
| `src/common/io/FileSystem.cpp` | Modified | `config_dir()` now checks `EA_CONFIG_DIR` env var before falling back to `home_dir()/.embedded-agent` |
| `tests/unit/test_filesystem.cpp` | Modified | Added 3 unit tests for `EA_CONFIG_DIR` behavior |

## Implementation Details

### config_dir() change

**Before:**
```cpp
Result<std::string> config_dir() {
    auto home = home_dir();
    if (!home.ok()) return home.error();
    return home.value() + "/.embedded-agent";
}
```

**After:**
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

Key behaviors:
- If `EA_CONFIG_DIR` is set and non-empty, its value is used (after `expand_tilde()`)
- If `EA_CONFIG_DIR` is unset or empty, falls back to `home_dir()/.embedded-agent`
- Tilde expansion in `EA_CONFIG_DIR` is supported via the `expand_tilde()` function from Task 1

### Tests added

1. **config_dir honors EA_CONFIG_DIR env var** — Sets `EA_CONFIG_DIR=/tmp/ea_test_config`, verifies `config_dir()` returns that exact path.
2. **config_dir expands ~ in EA_CONFIG_DIR** — Sets `EA_CONFIG_DIR=~/custom-config`, verifies tilde is expanded to `home_dir()/custom-config`.
3. **config_dir ignores empty EA_CONFIG_DIR** — Sets `EA_CONFIG_DIR=""`, verifies fallback to `home_dir()/.embedded-agent`.

All tests save and restore the original `EA_CONFIG_DIR` value.

## Test Results

**Command:**
```bash
/home/lsy/embedded-agent/build/tests/unit/ea-unit-tests "[filesystem]"
```

**Output:**
```
Filters: [filesystem]
Randomness seeded to: 2119046392
===============================================================================
All tests passed (43 assertions in 20 test cases)
```

All 20 filesystem test cases (including the 3 new ones) pass with 43 assertions total.

## Concerns

None. The implementation is straightforward, correctly handles the empty-string edge case, and integrates with the existing `expand_tilde()` from Task 1.
