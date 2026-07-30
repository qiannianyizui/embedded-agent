add_library(ea-memory OBJECT
    HolographicMemory.cpp
    EntityExtractor.cpp
    FtsSanitizer.cpp
    FactRetriever.cpp
    ScopedMemory.cpp
    MemoryManager.cpp
)
ea_target_compile_options(ea-memory)
target_include_directories(ea-memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-memory PUBLIC ea-base ea-io ea-log sqlite3)

# HRR vector similarity (conditionally linked)
if(EA_ENABLE_HRR)
    target_link_libraries(ea-memory PUBLIC ea-hrr)
endif()
