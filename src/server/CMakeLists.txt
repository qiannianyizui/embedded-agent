add_library(ea-server OBJECT
    SessionManager.cpp
    HttpServer.cpp
)
ea_target_compile_options(ea-server)
target_include_directories(ea-server PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-server PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json PRIVATE httplib::httplib)
