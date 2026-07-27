add_library(ea-net OBJECT
    HttpClient.cpp
    SseParser.cpp
    RetryPolicy.cpp
    TlsConfig.cpp
)
ea_target_compile_options(ea-net)
target_include_directories(ea-net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-net PUBLIC ea-base ea-io spdlog::spdlog PRIVATE httplib::httplib)
