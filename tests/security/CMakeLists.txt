# tests/security/CMakeLists.txt
# Security/fuzz tests — placeholder.
# Source files (test_policy_boundary.cpp, test_tool_isolation.cpp, etc.) do not exist yet.
# This directory is included only when EA_ENABLE_SECURITY_TESTS=ON.
#
# When security .cpp files are added, uncomment the block below:
#
# add_executable(ea-security-tests
#     test_policy_boundary.cpp
#     test_tool_isolation.cpp
#     test_plugin_sandbox.cpp
#     test_injection.cpp
#     test_fuzz_json.cpp
#     test_fuzz_config.cpp
#     test_fuzz_provider.cpp
# )
# target_link_libraries(ea-security-tests PRIVATE
#     ea-test-common
#     ea-common ea-config ea-memory ea-conversation ea-budget
#     ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
#     ea-server ea-plugin
#     nlohmann_json::nlohmann_json
#     httplib::httplib
#     Catch2::Catch2WithMain
#     ${CMAKE_DL_LIBS}
# )
# target_include_directories(ea-security-tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_BINARY_DIR}/include
# )
# target_compile_definitions(ea-security-tests PRIVATE
#     EA_MOCK_PLUGIN_PATH="${CMAKE_BINARY_DIR}/tests/plugins/mock_plugin/ea_mock_plugin.so"
#     CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
#     CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
# )
# include(CTest)
# add_test(NAME ea-security-tests COMMAND ea-security-tests)
