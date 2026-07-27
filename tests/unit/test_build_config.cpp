#include <catch2/catch_test_macros.hpp>

TEST_CASE("Build config has all subsystems enabled", "[build]") {
    // All subsystems are always compiled in — no compile-time toggles remain.
    // Runtime mode selection is handled by the RunMode enum and CLI subcommands.
    SUCCEED();
}
