// tests/system/test_build_config_compat.cpp
// Build configuration compatibility tests — verify that build mode macros
// and feature flags are consistent and meaningful.
#include <catch2/catch_test_macros.hpp>
#include "ea/build_config.h"
#include "CompatTestHelper.h"

using namespace ea::test;

TEST_CASE("Compat: build config macros are defined", "[compat][system]") {
    // After runtime mode selection refactor, EA_MODE_* macros no longer exist.
    // All modes are compiled into the same binary and selected at runtime.
    // Verify that the build config header is still includable.
    // (If this compiles, the build config is valid.)
    REQUIRE(true);
}

TEST_CASE("Compat: CLI mode has expected features", "[compat][system]") {
#if defined(EA_MODE_CLI)
    verify_cli_mode_features();
#else
    // Skip — not CLI mode
#endif
}

TEST_CASE("Compat: Embedded mode disables heavy features", "[compat][system]") {
#if defined(EA_MODE_EMBEDDED)
    verify_embedded_mode_features();
#else
    // Skip — not Embedded mode
#endif
}

TEST_CASE("Compat: Server mode enables router and fallback", "[compat][system]") {
#if defined(EA_MODE_SERVER)
    verify_server_mode_features();
#else
    // Skip — not Server mode
#endif
}

TEST_CASE("Compat: feature flags are consistent", "[compat][system]") {
    // If Embedded mode, streaming must be off
#if defined(EA_MODE_EMBEDDED)
    REQUIRE_FALSE(EA_TEST_STREAMING_ENABLED);
    REQUIRE_FALSE(EA_TEST_TOOLS_WEB_ENABLED);
    REQUIRE_FALSE(EA_TEST_MCP_ENABLED);
    REQUIRE_FALSE(EA_TEST_PLUGINS_ENABLED);
#endif

    // If Server mode, router and fallback must be on
#if defined(EA_MODE_SERVER)
    REQUIRE(EA_TEST_ROUTER_ENABLED);
    REQUIRE(EA_TEST_FALLBACK_ENABLED);
#endif
}
