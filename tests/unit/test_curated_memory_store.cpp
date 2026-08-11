#include <catch2/catch_test_macros.hpp>
#include "memory/CuratedMemoryStore.h"
#include <cstdio>
#include <filesystem>
#include <random>
#include <atomic>

using namespace ea::memory;

namespace {

std::string make_temp_dir() {
    static std::atomic<unsigned> counter{0};
    auto base = std::filesystem::temp_directory_path();
    std::random_device rd;
    auto p = base / ("ea_curated_" + std::to_string(rd() + counter.fetch_add(1)));
    std::filesystem::create_directories(p);
    return p.string();
}

}  // namespace

TEST_CASE("CuratedMemoryStore creates files and adds entries", "[memory]") {
    auto dir = make_temp_dir();
    CuratedMemoryConfig cfg;
    cfg.dir = dir;
    CuratedMemoryStore store(cfg);

    REQUIRE(store.open().ok());
    REQUIRE(store.empty(MemoryTarget::User));

    REQUIRE(store.add(MemoryTarget::User, "User name is lsy").ok());
    REQUIRE_FALSE(store.empty(MemoryTarget::User));

    auto entries = store.entries(MemoryTarget::User);
    REQUIRE(entries.ok());
    REQUIRE(entries.value().size() == 1);
    REQUIRE(entries.value()[0] == "User name is lsy");

    auto fmt = store.format_for_system_prompt(MemoryTarget::User);
    REQUIRE(fmt.find("- User name is lsy") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("CuratedMemoryStore persists across reload", "[memory]") {
    auto dir = make_temp_dir();
    CuratedMemoryConfig cfg;
    cfg.dir = dir;
    {
        CuratedMemoryStore store(cfg);
        REQUIRE(store.open().ok());
        REQUIRE(store.add(MemoryTarget::Memory, "Project uses CMake").ok());
    }
    {
        CuratedMemoryStore store(cfg);
        REQUIRE(store.open().ok());
        auto entries = store.entries(MemoryTarget::Memory);
        REQUIRE(entries.ok());
        REQUIRE(entries.value().size() == 1);
        REQUIRE(entries.value()[0] == "Project uses CMake");
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("CuratedMemoryStore remove by substring", "[memory]") {
    auto dir = make_temp_dir();
    CuratedMemoryConfig cfg;
    cfg.dir = dir;
    CuratedMemoryStore store(cfg);
    REQUIRE(store.open().ok());
    REQUIRE(store.add(MemoryTarget::User, "Name: lsy").ok());

    auto removed = store.remove(MemoryTarget::User, "lsy");
    REQUIRE(removed.ok());
    REQUIRE(removed.value());
    REQUIRE(store.empty(MemoryTarget::User));

    auto missing = store.remove(MemoryTarget::User, "nothing");
    REQUIRE(missing.ok());
    REQUIRE_FALSE(missing.value());
    std::filesystem::remove_all(dir);
}

TEST_CASE("CuratedMemoryStore enforces char limit", "[memory]") {
    auto dir = make_temp_dir();
    CuratedMemoryConfig cfg;
    cfg.dir = dir;
    cfg.user_char_limit = 20;
    CuratedMemoryStore store(cfg);
    REQUIRE(store.open().ok());

    REQUIRE(store.add(MemoryTarget::User, "short").ok());
    auto result = store.add(MemoryTarget::User, "this entry is much too long");
    REQUIRE_FALSE(result.ok());
    std::filesystem::remove_all(dir);
}
