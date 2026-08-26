// Unit tests for InputBar TUI component — focus, event handling, history
#include <catch2/catch_test_macros.hpp>
#include "tui/InputBar.h"
#include "tui/ChatArea.h"
#include "tui/StatusBar.h"
#include <ftxui/component/mouse.hpp>
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

TEST_CASE("InputBar has no right-side hint", "[tui]") {
    InputBar bar;

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("Enter to send") == std::string::npos);
}

TEST_CASE("InputBar shows the manual-mode hint below the input", "[tui]") {
    InputBar bar;
    REQUIRE(bar.mode() == "manual");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("manual mode") != std::string::npos);
    REQUIRE(out.find("accept edits on") == std::string::npos);
}

TEST_CASE("InputBar shows the plan-mode hint below the input", "[tui]") {
    InputBar bar;
    bar.set_mode("plan");
    REQUIRE(bar.mode() == "plan");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("plan mode on") != std::string::npos);
    REQUIRE(out.find("manual mode") == std::string::npos);
}

TEST_CASE("InputBar shows the accept-edits hint", "[tui]") {
    InputBar bar;
    bar.set_mode("acceptEdits");
    REQUIRE(bar.mode() == "acceptEdits");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("accept edits on") != std::string::npos);
}

TEST_CASE("InputBar shows the auto-mode hint", "[tui]") {
    InputBar bar;
    bar.set_mode("auto");
    REQUIRE(bar.mode() == "auto");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("auto mode") != std::string::npos);
    REQUIRE(out.find("manual mode") == std::string::npos);
}

TEST_CASE("InputBar mode hint coexists with command suggestions", "[tui]") {
    InputBar bar;
    bar.set_mode("plan");
    bar.set_commands({{"/clear", "Clear view", ""}});

    bar.component()->OnEvent(ftxui::Event::Character('/'));

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(8));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("plan mode on") != std::string::npos);
    REQUIRE(out.find("/clear") != std::string::npos);
}

TEST_CASE("InputBar mode hint shows the workspace path before the mode", "[tui]") {
    InputBar bar;
    bar.set_cwd("/home/lsy/embedded-agent");
    bar.set_mode("plan");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("/home/lsy/embedded-agent") != std::string::npos);
    REQUIRE(out.find("plan mode on") != std::string::npos);
}

TEST_CASE("InputBar mode hint omits the path separator when cwd is unset", "[tui]") {
    InputBar bar;
    bar.set_mode("manual");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("·") == std::string::npos);
    REQUIRE(out.find("manual mode") != std::string::npos);
}

TEST_CASE("InputBar row uses the light theme background", "[tui]") {
    InputBar bar;
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(3));
    ftxui::Render(screen,
        ftxui::vbox({bar.component()->Render()})
            | ftxui::bgcolor(ftxui::Color::RGB(13, 17, 23)));
    auto bg = screen.CellAt(1, 0).background_color;
    REQUIRE_FALSE(bg == ftxui::Color::Default);
    REQUIRE(bg == ftxui::Color::RGB(232, 234, 242));
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

TEST_CASE("InputBar shows command suggestions when typing /", "[tui]") {
    InputBar bar;
    bar.set_commands({
        {"/skills", "List available skills", ""},
        {"/resume", "Resume a conversation", ""},
        {"/clear", "Clear the chat view", ""},
    });

    bar.component()->OnEvent(ftxui::Event::Character('/'));
    bar.component()->OnEvent(ftxui::Event::Character('s'));

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();
    REQUIRE(out.find("/skills") != std::string::npos);
    REQUIRE(out.find("/resume") != std::string::npos);
}

TEST_CASE("InputBar executes selected command with Enter", "[tui]") {
    InputBar bar;
    bar.set_commands({
        {"/skills", "List available skills", ""},
        {"/resume", "Resume a conversation", ""},
    });

    std::string executed;
    bar.set_on_command([&](const std::string& cmd) { executed = cmd; });

    bar.component()->OnEvent(ftxui::Event::Character('/'));
    bar.component()->OnEvent(ftxui::Event::Character('s'));
    bar.component()->OnEvent(ftxui::Event::ArrowDown);
    bar.component()->OnEvent(ftxui::Event::Return);

    REQUIRE(executed == "/resume");
}

TEST_CASE("InputBar Tab completes the selected command", "[tui]") {
    InputBar bar;
    bar.set_commands({{"/skills", "List skills", ""}, {"/clear", "Clear view", ""}});

    bar.component()->OnEvent(ftxui::Event::Character('/'));
    bar.component()->OnEvent(ftxui::Event::Character('s'));
    bar.component()->OnEvent(ftxui::Event::Tab);

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, bar.component()->Render());
    REQUIRE(screen.ToString().find("/skills ") != std::string::npos);
}

TEST_CASE("InputBar Tab puts cursor after the trailing space", "[tui]") {
    InputBar bar;
    bar.set_commands({{"/skills", "List skills", ""}, {"/clear", "Clear view", ""}});

    std::string submitted;
    bar.set_on_submit([&](const std::string& s) { submitted = s; });

    bar.component()->OnEvent(ftxui::Event::Character('/'));
    bar.component()->OnEvent(ftxui::Event::Character('s'));
    bar.component()->OnEvent(ftxui::Event::Tab);
    bar.component()->OnEvent(ftxui::Event::Character('x'));
    bar.component()->OnEvent(ftxui::Event::Return);

    REQUIRE(submitted == "/skills x");
}

TEST_CASE("InputBar suggestions never clip long command names", "[tui]") {
    InputBar bar;
    bar.set_commands({
        {"/superpowers:brainstorming",
         "Use when the user wants to brainstorm ideas and refine a plan", ""},
        {"/clear", "Clear view", ""},
    });

    bar.component()->OnEvent(ftxui::Event::Character('/'));

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(12));
    ftxui::Render(screen, bar.component()->Render());
    std::string out = screen.ToString();
    REQUIRE(out.find("/superpowers:brainstorming") != std::string::npos);
}

TEST_CASE("InputBar command suggestions scroll with mouse wheel", "[tui]") {
    InputBar bar;
    std::vector<CommandEntry> commands;
    for (int i = 0; i < 10; ++i) {
        commands.push_back({"/cmd" + std::to_string(i),
                            "Command " + std::to_string(i), ""});
    }
    bar.set_commands(commands);
    bar.component()->OnEvent(ftxui::Event::Character('/'));

    auto render = [&] {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                            ftxui::Dimension::Fixed(12));
        ftxui::Render(screen, bar.component()->Render());
        return screen.ToString();
    };

    REQUIRE(render().find("/cmd0") != std::string::npos);
    REQUIRE(render().find("/cmd8") == std::string::npos);

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::WheelDown;
    mouse.x = 40;
    mouse.y = 10;
    bar.component()->OnEvent(ftxui::Event::Mouse("", mouse));

    auto out = render();
    REQUIRE(out.find("/cmd8") != std::string::npos);
    REQUIRE(out.find("/cmd0") == std::string::npos);
}
