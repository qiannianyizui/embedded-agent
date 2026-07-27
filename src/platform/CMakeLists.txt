add_library(ea-platform OBJECT Platform.cpp)
ea_target_compile_options(ea-platform)
target_include_directories(ea-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-platform PUBLIC ea-base ea-io)
