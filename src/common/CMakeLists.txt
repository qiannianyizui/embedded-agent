add_library(ea-common OBJECT
    io/Logger.cpp
    io/FileSystem.cpp
    io/Process.cpp
    net/TlsConfig.cpp
    net/HttpClient.cpp
    net/SseParser.cpp
    net/RetryPolicy.cpp
)
ea_target_compile_options(ea-common)
target_include_directories(ea-common PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-common PUBLIC spdlog::spdlog PRIVATE httplib::httplib)
