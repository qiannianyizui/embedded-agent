add_library(ea-security OBJECT SecurityPolicy.cpp)
target_include_directories(ea-security PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-security PUBLIC ea-core ea-common)
