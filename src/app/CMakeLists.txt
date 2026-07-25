add_library(ea-app OBJECT
    AppBuilder.cpp
    CliCommandRegistry.cpp
)
ea_target_compile_options(ea-app)
target_include_directories(ea-app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-app PUBLIC
    ea-core ea-common ea-platform ea-config
    ea-provider ea-tool ea-memory ea-conversation ea-budget
    ea-agent ea-mcp ea-security
)
