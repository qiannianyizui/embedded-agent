add_library(ea-plugin OBJECT
    PluginModule.cpp
    PluginManifest.cpp
)
ea_target_compile_options(ea-plugin)
target_include_directories(ea-plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ea-plugin PUBLIC ea-core ea-common toml11::toml11)
