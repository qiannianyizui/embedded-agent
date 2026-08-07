# tests/system/CMakeLists.txt
add_executable(ea-system-tests
    test_smoke.cpp
    test_build_config_compat.cpp
    test_usability.cpp
)
target_link_libraries(ea-system-tests PRIVATE
    ea-test-common
    ea-base ea-io ea-net ea-log ea-config ea-hrr ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-skill ea-agent ea-trace ea-mcp ea-platform ea-security
    nlohmann_json::nlohmann_json
    httplib::httplib
    PkgConfig::UUID PkgConfig::ZSTD
    Catch2::Catch2WithMain
)
target_include_directories(ea-system-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-system-tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
include(CTest)
add_test(NAME ea-system-tests COMMAND ea-system-tests)
