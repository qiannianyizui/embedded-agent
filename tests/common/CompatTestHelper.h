// tests/common/CompatTestHelper.h
#pragma once
#include "ea/build_config.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace ea::test {

// Helper macros: #cmakedefine produces empty definitions when ON,
// so we need to convert them to boolean expressions for Catch2 REQUIRE.
#if defined(EA_ENABLE_MEMORY)
    #define EA_TEST_MEMORY_ENABLED true
#else
    #define EA_TEST_MEMORY_ENABLED false
#endif

#if defined(EA_ENABLE_STREAMING)
    #define EA_TEST_STREAMING_ENABLED true
#else
    #define EA_TEST_STREAMING_ENABLED false
#endif

#if defined(EA_ENABLE_TOOLS_WEB)
    #define EA_TEST_TOOLS_WEB_ENABLED true
#else
    #define EA_TEST_TOOLS_WEB_ENABLED false
#endif

#if defined(EA_ENABLE_TOOLS_SHELL)
    #define EA_TEST_TOOLS_SHELL_ENABLED true
#else
    #define EA_TEST_TOOLS_SHELL_ENABLED false
#endif

#if defined(EA_ENABLE_ROUTER)
    #define EA_TEST_ROUTER_ENABLED true
#else
    #define EA_TEST_ROUTER_ENABLED false
#endif

#if defined(EA_ENABLE_FALLBACK)
    #define EA_TEST_FALLBACK_ENABLED true
#else
    #define EA_TEST_FALLBACK_ENABLED false
#endif

#if defined(EA_ENABLE_MCP)
    #define EA_TEST_MCP_ENABLED true
#else
    #define EA_TEST_MCP_ENABLED false
#endif

#if defined(EA_ENABLE_PLUGINS)
    #define EA_TEST_PLUGINS_ENABLED true
#else
    #define EA_TEST_PLUGINS_ENABLED false
#endif

// Verify feature toggle consistency under each build mode
inline void verify_cli_mode_features() {
#if defined(EA_MODE_CLI)
    REQUIRE(EA_TEST_MEMORY_ENABLED);
    REQUIRE(EA_TEST_STREAMING_ENABLED);
    REQUIRE(EA_TEST_TOOLS_SHELL_ENABLED);
    REQUIRE(EA_TEST_MCP_ENABLED);
    REQUIRE(EA_TEST_PLUGINS_ENABLED);
#endif
}

inline void verify_embedded_mode_features() {
#if defined(EA_MODE_EMBEDDED)
    REQUIRE_FALSE(EA_TEST_STREAMING_ENABLED);
    REQUIRE_FALSE(EA_TEST_TOOLS_WEB_ENABLED);
    REQUIRE_FALSE(EA_TEST_ROUTER_ENABLED);
    REQUIRE_FALSE(EA_TEST_FALLBACK_ENABLED);
    REQUIRE_FALSE(EA_TEST_MCP_ENABLED);
    REQUIRE_FALSE(EA_TEST_PLUGINS_ENABLED);
#endif
}

inline void verify_server_mode_features() {
#if defined(EA_MODE_SERVER)
    REQUIRE(EA_TEST_ROUTER_ENABLED);
    REQUIRE(EA_TEST_FALLBACK_ENABLED);
#endif
}

}  // namespace ea::test
