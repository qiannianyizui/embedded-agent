cmake_minimum_required(VERSION 3.16)
project(embedded-agent VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel" OR CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fno-exceptions -fno-rtti)
endif()

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
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/archive/v3.6.2.tar.gz
)

FetchContent_MakeAvailable(json toml11 spdlog cli11 httplib mbedtls)

add_subdirectory(src/common)

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
