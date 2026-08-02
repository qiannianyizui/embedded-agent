add_library(ea-tui OBJECT
    Theme.cpp
    Spinner.cpp
    Banner.cpp
    SetupWizard.cpp
    TuiApp.cpp
    ChatArea.cpp
    InputBar.cpp
    StatusBar.cpp
    TopBar.cpp
    ApprovalDialog.cpp
    CommandPalette.cpp
    SessionSidebar.cpp
    TuiEventListener.cpp
    TuiApprovalHandler.cpp
)
ea_target_compile_options(ea-tui)
target_include_directories(ea-tui PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(ea-tui PUBLIC ftxui::component ea-base ea-io ea-log ea-security)

if(EA_BUILD_TUI_PREVIEW)
    add_executable(tui-preview preview/tui_preview.cpp)
    ea_target_compile_options(tui-preview)
    target_include_directories(tui-preview PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(tui-preview PRIVATE
        ea-tui
        embedded-agent-core
        ftxui::component
        ftxui::screen
        ftxui::dom
    )
endif()
