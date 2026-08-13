# tests/blackbox/CMakeLists.txt
add_executable(ea-blackbox-tests
    test_protocol_compliance.cpp
    test_provider_contract.cpp
    test_tool_contract.cpp
    test_memory_contract.cpp
    test_property.cpp
    test_config_matrix.cpp
    test_fault_injection.cpp
)
target_link_libraries(ea-blackbox-tests PRIVATE
    ea-test-common
    ea-base ea-io ea-net ea-log ea-config ea-hrr ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-skill ea-agent ea-trace ea-mcp ea-platform ea-security
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
)
target_include_directories(ea-blackbox-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-blackbox-tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    EA_BINARY_PATH="${CMAKE_BINARY_DIR}/ea"
)
include(CTest)
add_test(NAME ea-blackbox-tests COMMAND ea-blackbox-tests)
