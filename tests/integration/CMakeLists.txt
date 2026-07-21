# tests/integration/CMakeLists.txt
add_executable(ea-integration-tests
    test_agent_memory.cpp
    test_provider_tool.cpp
    test_full_pipeline.cpp
    test_conversation_integration.cpp
)
target_link_libraries(ea-integration-tests PRIVATE
    ea-test-common
    ea-common ea-config ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
    ea-server ea-plugin
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_include_directories(ea-integration-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-integration-tests PRIVATE
    EA_MOCK_PLUGIN_PATH="${CMAKE_BINARY_DIR}/tests/plugins/mock_plugin/ea_mock_plugin.so"
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
include(CTest)
add_test(NAME ea-integration-tests COMMAND ea-integration-tests)
