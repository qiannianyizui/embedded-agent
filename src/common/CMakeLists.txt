add_library(ea-common OBJECT
    io/Logger.cpp
    io/FileSystem.cpp
    io/Process.cpp
)
target_include_directories(ea-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-common PRIVATE spdlog::spdlog)
