# tests/common/CMakeLists.txt
add_library(ea-test-common OBJECT
    MockProvider.cpp
    MockTool.cpp
    MockTransport.cpp
)
target_include_directories(ea-test-common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests/common
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-test-common PUBLIC
    ea-base ea-io ea-net
    ea-log
    ea-mcp
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)
