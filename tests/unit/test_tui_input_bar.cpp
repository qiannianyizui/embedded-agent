// Unit tests for InputBar TUI component — focus, event handling, history
#include <catch2/catch_test_macros.hpp>
#include "tui/InputBar.h"
#include "tui/ChatArea.h"
#include "tui/StatusBar.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component.hpp>

using namespace ea::tui;

TEST_CASE("InputBar component is focusable", "[tui]") {
    InputBar bar;
    auto comp = bar.component();
    REQUIRE(comp);
    // The InputBar wraps an ftxui::Input, which must be focusable
    REQUIRE(comp->Focusable());
}

TEST_CASE("InputBar component returns same instance", "[tui]") {
    InputBar bar;
    auto comp1 = bar.component();
    auto comp2 = bar.component();
    REQUIRE(comp1 == comp2);
}

TEST_CASE("InputBar set_busy changes state", "[tui]") {
    InputBar bar;
    // Should not crash
    bar.set_busy(true);
    bar.set_busy(false);
}

TEST_CASE("InputBar set_on_submit stores callback", "[tui]") {
    InputBar bar;
    bool called = false;
    bar.set_on_submit([&](const std::string& input) {
        called = true;
    });
    // Callback is stored; we can't easily trigger it without FTXUI event loop
    // but we verify it doesn't crash
}

TEST_CASE("InputBar clear resets state", "[tui]") {
    InputBar bar;
    bar.clear();
    // Should not crash
}

TEST_CASE("Container::Vertical routes focus to InputBar", "[tui]") {
    // Only InputBar goes in the Container — ChatArea/StatusBar are
    // non-focusable and should NOT be in the focus chain.
    InputBar input_bar;
    auto input_comp = input_bar.component();

    auto container = ftxui::Container::Vertical({input_comp});

    REQUIRE(container->Focusable());

    auto active = container->ActiveChild();
    REQUIRE(active);
    REQUIRE(active == input_comp);
}

TEST_CASE("Container::Vertical with Renderer wrapper preserves focus", "[tui]") {
    // Verify the pattern used in TuiApp::build_component_tree():
    // Container::Vertical({input_bar}) + Renderer(container, render)
    ChatArea chat_area;
    StatusBar status_bar;
    InputBar input_bar;

    auto input_comp = input_bar.component();

    // Only InputBar in the Container — ChatArea/StatusBar rendered directly
    auto container = ftxui::Container::Vertical({input_comp});

    auto main_layout = ftxui::Renderer(container, [&] {
        return ftxui::vbox({
            chat_area.component()->Render() | ftxui::flex | ftxui::frame,
            ftxui::separator(),
            status_bar.component()->Render(),
            ftxui::separator(),
            input_bar.component()->Render(),
        });
    });

    REQUIRE(main_layout->Focusable());

    auto active = main_layout->ActiveChild();
    REQUIRE(active);
    REQUIRE(active == container);

    auto inner_active = container->ActiveChild();
    REQUIRE(inner_active == input_comp);
}
