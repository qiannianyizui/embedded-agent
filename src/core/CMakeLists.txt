add_library(ea-core INTERFACE)
target_include_directories(ea-core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-core INTERFACE ea-common nlohmann_json::nlohmann_json)
