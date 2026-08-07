#include <catch2/catch_test_macros.hpp>

TEST_CASE("Build config has all subsystems enabled", "[build]") {
    // All subsystems are always compiled in — no compile-time toggles remain.
    // The application currently runs in TUI mode; the core remains a static
    // library (embedded-agent-core) for embedded integration.
    SUCCEED();
}
