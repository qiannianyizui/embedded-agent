// Unit tests for Theme system
#include <catch2/catch_test_macros.hpp>
#include "tui/Theme.h"

using namespace ea::tui;

TEST_CASE("default_theme returns consistent singleton", "[tui]") {
    const Theme& t1 = default_theme();
    const Theme& t2 = default_theme();
    REQUIRE(&t1 == &t2);
}

TEST_CASE("Theme colors match midnight palette hex values", "[tui]") {
    const auto& c = default_theme().color;
    // primary = #8B8CF8 = RGB(139, 140, 248)
    REQUIRE(c.primary == ftxui::Color::RGB(139, 140, 248));
    // accent = #4CC9F0 = RGB(76, 201, 240)
    REQUIRE(c.accent == ftxui::Color::RGB(76, 201, 240));
    // border = #2A3346 = RGB(42, 51, 70)
    REQUIRE(c.border == ftxui::Color::RGB(42, 51, 70));
    // error = #F87171 = RGB(248, 113, 113)
    REQUIRE(c.error == ftxui::Color::RGB(248, 113, 113));
}

TEST_CASE("Theme brand defaults", "[tui]") {
    const auto& b = default_theme().brand;
    REQUIRE(b.name == "Embedded Agent");
    REQUIRE(b.prompt == "❯");
    REQUIRE_FALSE(b.welcome.empty());
}

TEST_CASE("Role styles return correct chip labels", "[tui]") {
    const auto& theme = default_theme();
    REQUIRE(theme.role_user().label == "you");
    REQUIRE(theme.role_assistant().label == "agent");
    REQUIRE(theme.role_system().label == "system");
    REQUIRE(theme.role_tool().label == "tool");
    REQUIRE(theme.role_error().label == "error");
}

TEST_CASE("Semantic colors are distinct", "[tui]") {
    const auto& c = default_theme().color;
    REQUIRE_FALSE(c.ok == c.error);
    REQUIRE_FALSE(c.warn == c.error);
    REQUIRE_FALSE(c.primary == c.accent);
}
