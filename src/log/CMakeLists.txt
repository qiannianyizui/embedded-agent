add_library(ea-log OBJECT
    Logger.cpp
)
ea_target_compile_options(ea-log)
target_include_directories(ea-log PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ea-log PUBLIC
    ea-base
    spdlog::spdlog
    PkgConfig::ZSTD
)
