add_executable(ea-tests
    test_common_result.cpp
    test_common_string.cpp
    test_common_json.cpp
    test_common_sse.cpp
    test_config.cpp
    test_memory_sqlite.cpp
    test_provider_openai.cpp
    test_tool_registry.cpp
    test_tool_shell.cpp
    test_tool_file.cpp
)
target_link_libraries(ea-tests PRIVATE
    ea-common
    ea-config
    ea-memory
    ea-provider
    ea-tool
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_include_directories(ea-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(CTest)
add_test(NAME ea-tests COMMAND ea-tests)
