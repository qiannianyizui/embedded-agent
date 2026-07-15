add_library(ea-security OBJECT SecurityPolicy.cpp)
ea_target_compile_options(ea-security)
target_include_directories(ea-security PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-security PUBLIC ea-core ea-common)
