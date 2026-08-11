#include <catch2/catch_test_macros.hpp>
#include "app/onboarding.h"
#include "config/Config.h"
#include <cstdio>
#include <filesystem>
#include <random>
#include <atomic>

using namespace ea::app;

namespace {

std::string make_temp_path() {
    static std::atomic<unsigned> counter{0};
    auto base = std::filesystem::temp_directory_path();
    std::random_device rd;
    auto p = base / ("ea_onboarding_" + std::to_string(rd() + counter.fetch_add(1)) + ".toml");
    return p.string();
}

}  // namespace

TEST_CASE("onboarding should_offer_profile defaults", "[app][onboarding]") {
    ea::config::AppConfig cfg;
    REQUIRE(should_offer_profile(cfg));

    cfg.onboarding.profile_build = false;
    REQUIRE_FALSE(should_offer_profile(cfg));

    cfg.onboarding.profile_build = true;
    cfg.onboarding.profile_build_offered = true;
    REQUIRE_FALSE(should_offer_profile(cfg));
}

TEST_CASE("onboarding directive is Hermes-style", "[app][onboarding]") {
    auto d = profile_build_directive();
    REQUIRE(d.find("first message ever") != std::string::npos);
    REQUIRE(d.find("target=\"user\"") != std::string::npos);
}

TEST_CASE("mark_profile_offered persists latch", "[app][onboarding]") {
    auto path = make_temp_path();
    ea::config::AppConfig cfg;
    cfg.config_path = path;

    REQUIRE(mark_profile_offered(cfg).ok());

    auto loaded = ea::config::load(path);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().onboarding.profile_build_offered);
    REQUIRE_FALSE(should_offer_profile(loaded.value()));

    std::remove(path.c_str());
}
