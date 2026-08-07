add_library(ea-tool OBJECT
    ToolRegistry.cpp
    Toolset.cpp
    ShellTool.cpp
    FileTool.cpp
    SearchTool.cpp
    WebTool.cpp
    MemoryTool.cpp
    FactStoreTool.cpp
    FactFeedbackTool.cpp
    ToolOutputConfig.cpp
    SkillTools.cpp
)
ea_target_compile_options(ea-tool)
target_include_directories(ea-tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-tool PUBLIC ea-base ea-io ea-net ea-log ea-skill)
