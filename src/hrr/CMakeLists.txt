add_library(ea-hrr OBJECT HrrVector.cpp)
ea_target_compile_options(ea-hrr)
target_include_directories(ea-hrr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# mbedcrypto provides SHA-256 for deterministic phase encoding.
# The include dirs and link flags propagate through the mbedtls static lib.
target_link_libraries(ea-hrr PUBLIC ea-base)
# Link mbedcrypto directly — it provides mbedtls_sha256()
target_link_libraries(ea-hrr PUBLIC mbedcrypto)
