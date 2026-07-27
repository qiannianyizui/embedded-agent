add_library(ea-base INTERFACE)
target_include_directories(ea-base INTERFACE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ea-base INTERFACE nlohmann_json::nlohmann_json)
