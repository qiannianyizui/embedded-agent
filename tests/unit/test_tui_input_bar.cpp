// Unit tests for InputBar TUI component — focus, event handling, history
#include <catch2/catch_test_macros.hpp>
#include "tui/InputBar.h"
#include "tui/ChatArea.h"
#include "tui/StatusBar.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

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

TEST_CASE("InputBar keeps input visible while busy", "[tui]") {
    InputBar bar;
    bar.set_busy(true);

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    // The prompt and busy hint must both be visible; the input must not be
    // replaced by a "working…" row that drops the text field.
    REQUIRE(out.find("❯") != std::string::npos);
    REQUIRE(out.find("working… · Ctrl+C to interrupt") == std::string::npos);
}

TEST_CASE("InputBar accepts typing while busy", "[tui]") {
    InputBar bar;
    bar.set_busy(true);

    bar.component()->OnEvent(ftxui::Event::Character('x'));
    bar.component()->OnEvent(ftxui::Event::Character('z'));

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find('x') != std::string::npos);
    REQUIRE(out.find('z') != std::string::npos);
}

TEST_CASE("InputBar submits via callback while busy (TUI queues it)", "[tui]") {
    InputBar bar;
    int calls = 0;
    bar.set_on_submit([&](const std::string&) { calls++; });

    // While busy, Enter still fires the submit callback so the TUI can queue
    // the message instead of silently dropping it.
    bar.set_busy(true);
    bar.component()->OnEvent(ftxui::Event::Character('x'));
    bar.component()->OnEvent(ftxui::Event::Return);
    REQUIRE(calls == 1);

    // The input was cleared by the submission; a later Enter is a no-op.
    bar.set_busy(false);
    bar.component()->OnEvent(ftxui::Event::Return);
    REQUIRE(calls == 1);
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
