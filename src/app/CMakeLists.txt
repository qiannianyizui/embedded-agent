add_library(ea-app OBJECT
    AppBuilder.cpp
    CliCommandRegistry.cpp
    auto_resume.cpp
    IRunner.cpp
    CliRunner.cpp
    ServerRunner.cpp
    SetupWizardRunner.cpp
    TuiRunner.cpp
)

target_link_libraries(ea-app PUBLIC ea-tui)

ea_target_compile_options(ea-app)
target_include_directories(ea-app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-app PUBLIC
    ea-base ea-io ea-net ea-log ea-platform ea-config
    ea-provider ea-tool ea-memory ea-conversation ea-budget
    ea-agent ea-mcp ea-security
)
