cmake_minimum_required(VERSION 3.16)
project(embedded-agent VERSION 0.1.0 LANGUAGES C CXX)

# Suppress DOWNLOAD_EXTRACT_TIMESTAMP dev warnings from FetchContent
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Generate build config header
configure_file(
    ${CMAKE_SOURCE_DIR}/include/ea/build_config.h.in
    ${CMAKE_BINARY_DIR}/include/ea/build_config.h
)

# Project-wide warnings; RTTI disabled per-target for release builds
add_compile_options(-Wall -Wextra -Wpedantic)

# Disable RTTI only for project C++ targets in release builds (saves binary size).
# NOTE: -fno-exceptions is NOT used because spdlog requires C++ exceptions.
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
    set(EA_RELEASE_FLAGS -fno-rtti)
endif()

function(ea_target_compile_options target)
    target_compile_options(${target} PRIVATE ${EA_RELEASE_FLAGS})
endfunction()

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party")

# --- header-only ---
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_Declare(toml11
    URL https://github.com/ToruNiina/toml11/archive/v4.2.0.tar.gz
)
FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/v1.15.0.tar.gz
)
FetchContent_Declare(cli11
    URL https://github.com/CLIUtils/CLI11/archive/v2.4.2.tar.gz
)
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/v0.18.3.tar.gz
)

# --- compiled ---
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/archive/v3.6.2.tar.gz
    PATCH_COMMAND bash -c "mkdir -p framework && echo '# stub' > framework/CMakeLists.txt"
)

FetchContent_MakeAvailable(json toml11 spdlog cli11 httplib mbedtls)

# --- zstd (system-installed, for compressed log sink) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD IMPORTED_TARGET libzstd)
endif()
if(NOT ZSTD_FOUND)
    find_package(zstd REQUIRED)
endif()

# --- libuuid (system-installed, for trace event IDs) ---
if(PkgConfig_FOUND)
    pkg_check_modules(UUID IMPORTED_TARGET uuid)
endif()
if(NOT UUID_FOUND)
    find_package(uuid REQUIRED)
endif()

# --- FTXUI (TUI library) ---
FetchContent_Declare(ftxui
    URL https://github.com/ArthurSonzogni/FTXUI/archive/v7.0.1.tar.gz
)
FetchContent_MakeAvailable(ftxui)

# --- Eigen (linear algebra for HRR vector operations) ---
FetchContent_Declare(eigen
    URL https://gitlab.com/libeigen/Eigen/-/archive/3.4.0/Eigen-3.4.0.tar.gz
)
FetchContent_MakeAvailable(eigen)

add_subdirectory(src/base)
add_subdirectory(src/io)
add_subdirectory(src/net)
add_subdirectory(src/log)
add_subdirectory(src/config)
add_subdirectory(src/platform)
add_subdirectory(src/security)
add_subdirectory(src/hrr)
add_subdirectory(src/memory)
add_subdirectory(src/conversation)
add_subdirectory(src/budget)
add_subdirectory(src/provider)
add_subdirectory(src/tool)
add_subdirectory(src/agent)
add_subdirectory(src/trace)
add_subdirectory(src/mcp)
add_subdirectory(src/plugin)
add_subdirectory(src/server)
add_subdirectory(src/app)

add_subdirectory(src/tui)

option(ENABLE_TESTS "Build tests" OFF)
if(ENABLE_TESTS)
    FetchContent_Declare(catch2
        URL https://github.com/catchorg/Catch2/archive/v3.7.1.tar.gz
    )
    FetchContent_MakeAvailable(catch2)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- SQLite amalgamation ---
set(SQLITE3_DIR "${CMAKE_SOURCE_DIR}/third_party/sqlite3")
set(SQLITE3_SRC "${SQLITE3_DIR}/sqlite3.c")
set(SQLITE3_INC "${SQLITE3_DIR}")
if(NOT EXISTS ${SQLITE3_SRC})
    file(DOWNLOAD
        https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
        "${CMAKE_SOURCE_DIR}/third_party/sqlite3.zip"
    )
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar x
        "${CMAKE_SOURCE_DIR}/third_party/sqlite3.zip"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party"
    )
    file(RENAME
        "${CMAKE_SOURCE_DIR}/third_party/sqlite-amalgamation-3460000"
        "${SQLITE3_DIR}"
    )
endif()

add_library(sqlite3 STATIC ${SQLITE3_SRC})
target_include_directories(sqlite3 PUBLIC ${SQLITE3_INC})
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_JOURNAL_MODE_WAL
)

# --- final targets ---
add_library(embedded-agent-core STATIC
    $<TARGET_OBJECTS:ea-io>
    $<TARGET_OBJECTS:ea-net>
    $<TARGET_OBJECTS:ea-log>
    $<TARGET_OBJECTS:ea-provider>
    $<TARGET_OBJECTS:ea-tool>
    $<TARGET_OBJECTS:ea-memory>
    $<TARGET_OBJECTS:ea-conversation>
    $<TARGET_OBJECTS:ea-budget>
    $<TARGET_OBJECTS:ea-agent>
    $<TARGET_OBJECTS:ea-trace>
    $<TARGET_OBJECTS:ea-mcp>
    $<TARGET_OBJECTS:ea-platform>
    $<TARGET_OBJECTS:ea-config>
    $<TARGET_OBJECTS:ea-security>
    $<TARGET_OBJECTS:ea-server>
    $<TARGET_OBJECTS:ea-plugin>
    $<TARGET_OBJECTS:ea-app>
    $<TARGET_OBJECTS:ea-hrr>
)
target_link_libraries(embedded-agent-core PUBLIC
    ea-base
    ea-io
    ea-net
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
    mbedtls
    mbedx509
    mbedcrypto
    sqlite3
    PkgConfig::UUID
    PkgConfig::ZSTD
)
# Eigen (always linked for HRR vector operations)
target_link_libraries(embedded-agent-core PUBLIC Eigen3::Eigen)
target_include_directories(embedded-agent-core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

add_executable(embedded-agent src/main.cpp)
ea_target_compile_options(embedded-agent)
target_link_libraries(embedded-agent PRIVATE
    embedded-agent-core CLI11::CLI11 ea-tui ftxui::component)
# Export symbols for dlopen-based plugin loading
set_target_properties(embedded-agent PROPERTIES ENABLE_EXPORTS ON)
