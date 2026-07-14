add_library(ea-platform OBJECT Platform.cpp)
target_include_directories(ea-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-platform PUBLIC ea-common)
