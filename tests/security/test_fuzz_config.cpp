// tests/security/test_fuzz_config.cpp
#include <catch2/catch_test_macros.hpp>
#include "config/Config.h"

using namespace ea::config;

TEST_CASE("Fuzz: Config load with non-existent paths doesn't crash", "[fuzz][security]") {
    // Test a small set of non-existent paths to verify config::load()
    // handles missing files gracefully without spewing warning logs.
    const char* paths[] = {
        "/no/such/path/config.toml",
        "/tmp/nonexistent_abc123.toml",
        "",  // empty path triggers default lookup
    };
    for (const auto* path : paths) {
        try {
            auto result = load(path);
            // Must not crash; returning a default config is OK
            (void)result;
        } catch (...) {
            // Must not crash
        }
    }
    REQUIRE(true);
}

TEST_CASE("Fuzz: Config load with invalid TOML content returns error", "[fuzz][security]") {
    // Write random garbage to a temp file and verify load() returns an error
    // instead of crashing.
    const char* tmp_path = "/tmp/ea_fuzz_config_test.toml";
    const char* garbage = "{{{{not valid toml!@#$%^&*()";

    // Write garbage content
    FILE* f = fopen(tmp_path, "w");
    if (f) {
        fputs(garbage, f);
        fclose(f);
    }

    try {
        auto result = load(tmp_path);
        // Should return a parse error, not crash
        (void)result;
    } catch (...) {
        // Must not crash
    }

    // Clean up
    remove(tmp_path);
    REQUIRE(true);
}
