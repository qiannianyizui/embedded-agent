add_library(ea-io OBJECT
    FileSystem.cpp
    Process.cpp
)
ea_target_compile_options(ea-io)
target_include_directories(ea-io PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-io PUBLIC ea-base ea-log spdlog::spdlog)
