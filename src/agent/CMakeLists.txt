add_library(ea-agent OBJECT
    AgentLoop.cpp
    SystemPrompt.cpp
    LoopDetector.cpp
    ContextCompressor.cpp
    steps/CallProviderStep.cpp
    steps/LoopDetectStep.cpp
    steps/ExecuteToolsStep.cpp
)
ea_target_compile_options(ea-agent)
target_include_directories(ea-agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-agent PUBLIC ea-core ea-common ea-platform ea-tool)
