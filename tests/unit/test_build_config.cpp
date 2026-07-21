#include <catch2/catch_test_macros.hpp>
#include "ea/build_config.h"

TEST_CASE("Build config defines at least one mode", "[build]") {
#if !defined(EA_MODE_CLI) && !defined(EA_MODE_EMBEDDED) && !defined(EA_MODE_SERVER)
    FAIL("No build mode defined");
#else
    SUCCEED();
#endif
}

TEST_CASE("Build config memory is enabled by default", "[build]") {
#ifdef EA_ENABLE_MEMORY
    SUCCEED();
#else
    FAIL("EA_ENABLE_MEMORY should be ON by default");
#endif
}

TEST_CASE("Build config streaming is enabled in CLI mode", "[build]") {
#ifdef EA_MODE_CLI
    #ifdef EA_ENABLE_STREAMING
        SUCCEED();
    #else
        FAIL("EA_ENABLE_STREAMING should be ON in CLI mode");
    #endif
#else
    SUCCEED();  // Not CLI mode, skip
#endif
}
