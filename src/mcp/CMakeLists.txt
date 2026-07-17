add_library(ea-mcp OBJECT
    StdioTransport.cpp
    McpClient.cpp
)
ea_target_compile_options(ea-mcp)
target_include_directories(ea-mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-mcp PUBLIC ea-core ea-common)
