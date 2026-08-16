// Unit tests for TuiApp global event routing and test accessors
#include <catch2/catch_test_macros.hpp>
#include "tui/TuiApp.h"
#include <ftxui/component/mouse.hpp>

using namespace ea::tui;

TEST_CASE("TuiApp routes mouse wheel to chat scroll", "[tui]") {
    TuiApp app;
    auto& chat = app.chat_area_for_test();
    chat.set_viewport_hint(3);
    for (int i = 0; i < 10; ++i) {
        chat.append_user("msg " + std::to_string(i));
    }

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::WheelUp;
    mouse.x = 40;
    mouse.y = 10;

    REQUIRE(app.handle_global_event(ftxui::Event::Mouse("", mouse)));
    REQUIRE(chat.scroll_top() < 40);
    REQUIRE_FALSE(chat.follow_bottom());
}
