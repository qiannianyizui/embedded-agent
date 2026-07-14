add_executable(ea-tests
    test_common_result.cpp
    test_common_string.cpp
    test_common_json.cpp
    test_common_sse.cpp
    test_config.cpp
    test_memory_sqlite.cpp
)
target_link_libraries(ea-tests PRIVATE
    ea-common
    ea-config
    ea-memory
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_include_directories(ea-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(CTest)
add_test(NAME ea-tests COMMAND ea-tests)
