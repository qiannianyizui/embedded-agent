add_library(ea-tui OBJECT
    Theme.cpp
    TuiApp.cpp
    ChatArea.cpp
    InputBar.cpp
    StatusBar.cpp
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
target_link_libraries(ea-tui PUBLIC ftxui::component ea-core ea-security)
