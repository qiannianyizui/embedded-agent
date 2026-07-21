# tests/bench/CMakeLists.txt
# Benchmark tests — only included when EA_ENABLE_BENCH_TESTS=ON.
add_executable(ea-bench-tests
    bench_agent_loop.cpp
    bench_memory.cpp
    bench_tool_execute.cpp
    bench_provider_parse.cpp
    bench_concurrent.cpp
)
target_link_libraries(ea-bench-tests PRIVATE
    ea-test-common
    ea-common ea-config ea-memory ea-conversation ea-budget
    ea-provider ea-tool ea-agent ea-mcp ea-platform ea-security
    ea-server ea-plugin
    nlohmann_json::nlohmann_json
    httplib::httplib
    Catch2::Catch2WithMain
    ${CMAKE_DL_LIBS}
)
target_include_directories(ea-bench-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
target_compile_definitions(ea-bench-tests PRIVATE
    CATCH_CONFIG_ENABLE_BENCHMARKING
)
include(CTest)
add_test(NAME ea-bench-tests COMMAND ea-bench-tests --benchmark-samples 50)
