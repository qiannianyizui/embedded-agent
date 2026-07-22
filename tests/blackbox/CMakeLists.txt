# tests/blackbox/CMakeLists.txt
add_executable(ea-blackbox-tests
    test_cli_e2e.cpp
    test_http_api_e2e.cpp
    test_provider_contract.cpp
    test_tool_contract.cpp
    test_memory_contract.cpp
    test_protocol_compliance.cpp
    test_fault_injection.cpp
    test_property.cpp
    test_config_matrix.cpp
)
target_link_libraries(ea-blackbox-tests PRIVATE
    ea-test-common
    ea-common ea-config ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
    ea-server ea-plugin
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_include_directories(ea-blackbox-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(ea-blackbox-tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    EA_MOCK_PLUGIN_PATH="${CMAKE_BINARY_DIR}/tests/plugins/mock_plugin/ea_mock_plugin.so"
)
add_dependencies(ea-blackbox-tests embedded-agent)
include(CTest)
add_test(NAME ea-blackbox-tests COMMAND ea-blackbox-tests)
