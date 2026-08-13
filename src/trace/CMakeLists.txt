add_library(ea-trace OBJECT
    TraceEvent.cpp
    JsonlWriter.cpp
    TraceEventListener.cpp
    Trace.cpp
)
ea_target_compile_options(ea-trace)
target_include_directories(ea-trace PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ea-trace PUBLIC ea-base ea-log)
