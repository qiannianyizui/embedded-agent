add_library(ea_mock_plugin MODULE
    MockPlugin.cpp
)
target_include_directories(ea_mock_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)
# MODULE library: don't link against the host — plugins are loaded via dlopen
set_target_properties(ea_mock_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ea_mock_plugin"
)
