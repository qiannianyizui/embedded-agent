add_executable(ea-tests
    test_common_result.cpp
    test_common_string.cpp
    test_common_json.cpp
)
target_link_libraries(ea-tests PRIVATE
    ea-common
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
target_include_directories(ea-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(CTest)
add_test(NAME ea-tests COMMAND ea-tests)
