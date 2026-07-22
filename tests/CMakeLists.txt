# tests/CMakeLists.txt
# NOTE: ENABLE_TESTS option, Catch2 FetchContent, and enable_testing() are
# declared in the root CMakeLists.txt.  This file is only evaluated when
# ENABLE_TESTS is ON.

# Shared test infrastructure
add_subdirectory(common)

# Layered test binaries
option(EA_ENABLE_UNIT_TESTS       "Build unit tests"       ON)
option(EA_ENABLE_INTEGRATION_TESTS "Build integration tests" ON)
option(EA_ENABLE_SYSTEM_TESTS     "Build system tests"     ON)
option(EA_ENABLE_BENCH_TESTS      "Build benchmark tests"  OFF)
option(EA_ENABLE_SECURITY_TESTS   "Build security tests"   ON)
option(EA_ENABLE_BLACKBOX_TESTS   "Build blackbox/greybox tests" ON)

if(EA_ENABLE_UNIT_TESTS)
    add_subdirectory(unit)
endif()
if(EA_ENABLE_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
if(EA_ENABLE_SYSTEM_TESTS)
    add_subdirectory(system)
endif()
if(EA_ENABLE_BENCH_TESTS)
    add_subdirectory(bench)
endif()
if(EA_ENABLE_SECURITY_TESTS)
    add_subdirectory(security)
endif()
if(EA_ENABLE_BLACKBOX_TESTS)
    add_subdirectory(blackbox)
endif()

# Mock plugin for loader tests
add_subdirectory(plugins/mock_plugin)
