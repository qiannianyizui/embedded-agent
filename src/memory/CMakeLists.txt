add_library(ea-memory OBJECT SqliteMemory.cpp)
target_include_directories(ea-memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-memory PUBLIC ea-core ea-common sqlite3)
