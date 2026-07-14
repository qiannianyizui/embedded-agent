add_library(ea-agent OBJECT AgentLoop.cpp SystemPrompt.cpp)
target_include_directories(ea-agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-agent PUBLIC ea-core ea-common ea-platform ea-tool)
