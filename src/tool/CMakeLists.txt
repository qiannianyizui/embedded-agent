add_library(ea-tool OBJECT
    ToolRegistry.cpp
    Toolset.cpp
    ShellTool.cpp
    FileTool.cpp
    SearchTool.cpp
    WebTool.cpp
    MemoryTool.cpp
    ToolOutputConfig.cpp
)
ea_target_compile_options(ea-tool)
target_include_directories(ea-tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-tool PUBLIC ea-core ea-common)
