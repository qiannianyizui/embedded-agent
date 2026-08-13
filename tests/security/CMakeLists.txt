# tests/security/CMakeLists.txt
# Security/fuzz tests for the embedded-agent project.

add_executable(ea-security-tests
    test_policy_boundary.cpp
    test_tool_isolation.cpp
    test_injection.cpp
    test_fuzz_json.cpp
    test_fuzz_config.cpp
    test_fuzz_provider.cpp
)
target_link_libraries(ea-security-tests PRIVATE
    ea-test-common
    ea-base ea-io ea-net ea-log ea-config ea-hrr ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-skill ea-agent ea-trace ea-mcp ea-platform ea-security
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
)
target_include_directories(ea-security-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-security-tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
include(CTest)
add_test(NAME ea-security-tests COMMAND ea-security-tests)
