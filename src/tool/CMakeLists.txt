add_library(ea-tool OBJECT
    ToolRegistry.cpp
    ShellTool.cpp
    FileTool.cpp
    SearchTool.cpp
    WebTool.cpp
    MemoryTool.cpp
)
target_include_directories(ea-tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-tool PUBLIC ea-core ea-common)
