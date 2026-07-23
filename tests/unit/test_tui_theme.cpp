// Unit tests for Theme system
#include <catch2/catch_test_macros.hpp>
#include "tui/Theme.h"

using namespace ea::tui;

TEST_CASE("default_theme returns consistent singleton", "[tui]") {
    const Theme& t1 = default_theme();
    const Theme& t2 = default_theme();
    REQUIRE(&t1 == &t2);
}

TEST_CASE("Theme colors match Hermes hex values", "[tui]") {
    const auto& c = default_theme().color;
    // Verify a few key colors by checking RGB components
    // primary = #FFD700 = RGB(255, 215, 0)
    REQUIRE(c.primary == ftxui::Color::RGB(255, 215, 0));
    // accent = #FFBF00 = RGB(255, 191, 0)
    REQUIRE(c.accent == ftxui::Color::RGB(255, 191, 0));
    // border = #CD7F32 = RGB(205, 127, 50)
    REQUIRE(c.border == ftxui::Color::RGB(205, 127, 50));
    // error = #ef5350 = RGB(239, 83, 80)
    REQUIRE(c.error == ftxui::Color::RGB(239, 83, 80));
}

TEST_CASE("Theme brand defaults", "[tui]") {
    const auto& b = default_theme().brand;
    REQUIRE(b.name == "Embedded Agent");
    REQUIRE(b.prompt == "❯");
    REQUIRE(b.tool_prefix == "┊");
    REQUIRE_FALSE(b.welcome.empty());
}

TEST_CASE("Role styles return correct glyphs", "[tui]") {
    const auto& theme = default_theme();
    REQUIRE(theme.role_user().glyph == U'❯');
    REQUIRE(theme.role_assistant().glyph == U'┊');
    REQUIRE(theme.role_system().glyph == U'·');
    REQUIRE(theme.role_tool().glyph == U'⚡');
}

TEST_CASE("User role is bold", "[tui]") {
    REQUIRE(default_theme().role_user().bold == true);
    REQUIRE(default_theme().role_assistant().bold == false);
    REQUIRE(default_theme().role_system().bold == false);
    REQUIRE(default_theme().role_tool().bold == false);
}
