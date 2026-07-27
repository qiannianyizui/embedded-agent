add_library(ea-config OBJECT Config.cpp)
ea_target_compile_options(ea-config)
target_include_directories(ea-config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-config PUBLIC ea-base ea-io ea-net ea-log ea-agent ea-budget toml11::toml11)
