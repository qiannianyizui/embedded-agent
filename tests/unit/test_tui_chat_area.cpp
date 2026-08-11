// Unit tests for ChatArea TUI component
#include <catch2/catch_test_macros.hpp>
#include "tui/ChatArea.h"
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace ea::tui;

namespace {

ftxui::Event wheel_event(ftxui::Mouse::Button button) {
    ftxui::Mouse mouse;
    mouse.button = button;
    mouse.x = 40;
    mouse.y = 10;
    return ftxui::Event::Mouse("", mouse);
}

}  // namespace

TEST_CASE("ChatArea appends user message", "[tui]") {
    ChatArea area;
    area.append_user("Hello");
    REQUIRE(area.has_new_messages());
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea appends assistant message", "[tui]") {
    ChatArea area;
    area.append_assistant("Hi there");
    REQUIRE(area.has_new_messages());
}

TEST_CASE("ChatArea accumulates stream chunks", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;
    chunk.data = "Hello";
    area.append_stream_chunk(chunk);
    REQUIRE(area.has_new_messages());

    chunk.data = " world";
    area.append_stream_chunk(chunk);
    REQUIRE(area.streaming_content() == "Hello world");

    // After finish, streaming content becomes a full message
    area.finish_message();
    REQUIRE(area.has_new_messages());
    REQUIRE(area.streaming_content().empty());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].role == ea::Role::Assistant);
    REQUIRE(area.messages()[0].content == "Hello world");
}

TEST_CASE("ChatArea stream error chunk", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Error;
    chunk.data = "connection lost";
    area.append_stream_chunk(chunk);
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].is_error);
}

TEST_CASE("ChatArea clear removes all messages", "[tui]") {
    ChatArea area;
    area.append_user("test");
    area.clear();
    REQUIRE_FALSE(area.has_new_messages());
    REQUIRE(area.messages().empty());
    REQUIRE(area.streaming_content().empty());
}

TEST_CASE("ChatArea tool messages", "[tui]") {
    ChatArea area;
    area.append_tool_start("shell", R"({"command":"ls"})");
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].role == ea::Role::Tool);
    REQUIRE(area.messages()[0].tool_name == "shell");

    area.clear_new_flag();
    area.append_tool_end("shell", "file1\nfile2", false);
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 2);
    REQUIRE_FALSE(area.messages()[1].is_error);

    // Tool end with error
    area.append_tool_end("shell", "command failed", true);
    REQUIRE(area.messages().back().is_error);
}

TEST_CASE("ChatArea error messages", "[tui]") {
    ChatArea area;
    area.append_error("Something went wrong");
    REQUIRE(area.has_new_messages());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].is_error);
    REQUIRE(area.messages()[0].content == "Something went wrong");
}

TEST_CASE("ChatArea component returns valid FTXUI component", "[tui]") {
    ChatArea area;
    auto comp = area.component();
    REQUIRE(comp);
    // Calling component() again returns the same instance
    auto comp2 = area.component();
    REQUIRE(comp == comp2);
}

TEST_CASE("ChatArea streaming then finish preserves content", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;

    chunk.data = "Line 1\n";
    area.append_stream_chunk(chunk);
    chunk.data = "Line 2\n";
    area.append_stream_chunk(chunk);
    chunk.data = "Line 3";
    area.append_stream_chunk(chunk);

    REQUIRE(area.streaming_content() == "Line 1\nLine 2\nLine 3");

    area.finish_message();
    REQUIRE(area.streaming_content().empty());
    REQUIRE(area.messages().size() == 1);
    REQUIRE(area.messages()[0].content == "Line 1\nLine 2\nLine 3");
    REQUIRE(area.messages()[0].role == ea::Role::Assistant);
}

TEST_CASE("ChatArea finish_message with empty buffer is no-op", "[tui]") {
    ChatArea area;
    area.finish_message();
    REQUIRE_FALSE(area.has_new_messages());
    REQUIRE(area.messages().empty());
}

TEST_CASE("ChatArea multiple message types coexist", "[tui]") {
    ChatArea area;
    area.append_user("What is 2+2?");
    area.append_assistant("Let me calculate that.");
    area.append_tool_start("calc", R"({"expr":"2+2"})");
    area.append_tool_end("calc", "4", false);
    area.append_assistant("The answer is 4.");

    REQUIRE(area.messages().size() == 5);
    REQUIRE(area.messages()[0].role == ea::Role::User);
    REQUIRE(area.messages()[1].role == ea::Role::Assistant);
    REQUIRE(area.messages()[2].role == ea::Role::Tool);
    REQUIRE(area.messages()[3].role == ea::Role::Tool);
    REQUIRE(area.messages()[4].role == ea::Role::Assistant);
}

TEST_CASE("ChatArea clear_new_flag idempotent", "[tui]") {
    ChatArea area;
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
    area.clear_new_flag();  // Should not crash
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea wheel scrolls transcript", "[tui]") {
    ChatArea area;
    for (int i = 0; i < 10; ++i) {
        area.append_user("msg " + std::to_string(i));
    }

    REQUIRE(area.scroll_y() == 1.0f);
    REQUIRE(area.follow_bottom());

    REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelUp)));
    REQUIRE(area.scroll_y() < 1.0f);
    REQUIRE_FALSE(area.follow_bottom());

    REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelUp)));
    REQUIRE(area.scroll_y() < 0.9f);

    // Scrolling back to the bottom re-enables follow.
    while (area.scroll_y() < 1.0f) {
        REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelDown)));
    }
    REQUIRE(area.scroll_y() == 1.0f);
    REQUIRE(area.follow_bottom());
}

TEST_CASE("ChatArea follows bottom on new messages unless scrolled up", "[tui]") {
    ChatArea area;
    area.append_user("first");

    area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    area.append_assistant("second");
    REQUIRE(area.scroll_y() < 1.0f);  // Stay where the user scrolled.

    while (area.scroll_y() < 1.0f) {
        area.on_event(wheel_event(ftxui::Mouse::WheelDown));
    }
    area.append_user("third");
    REQUIRE(area.scroll_y() == 1.0f);  // Follow the newest message again.
}

TEST_CASE("ChatArea viewport follows bottom and scrolls up", "[tui]") {
    ChatArea area;
    for (int i = 0; i < 20; ++i) {
        area.append_user("message number " + std::to_string(i));
    }

    auto render = [&] {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                            ftxui::Dimension::Fixed(8));
        ftxui::Render(screen,
                      area.component()->Render() | ftxui::flex
                          | ftxui::yframe | ftxui::vscroll_indicator);
        return screen.ToString();
    };

    auto bottom = render();
    REQUIRE(bottom.find("message number 19") != std::string::npos);
    REQUIRE(bottom.find("message number 0") == std::string::npos);

    for (int i = 0; i < 15; ++i) {
        area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    }

    auto top = render();
    REQUIRE(top.find("message number 0") != std::string::npos);
    REQUIRE(top.find("message number 19") == std::string::npos);
}

TEST_CASE("ChatArea bottom-follows inside full TUI layout", "[tui]") {
    ChatArea area;
    for (int i = 0; i < 15; ++i) {
        area.append_user("history message " + std::to_string(i + 1));
    }

    auto render = [&] {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                            ftxui::Dimension::Fixed(24));
        ftxui::Element layout = ftxui::vbox({
            ftxui::text("top bar") | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1),
            ftxui::separator(),
            area.component()->Render() | ftxui::flex | ftxui::yframe
                | ftxui::vscroll_indicator,
            ftxui::separator(),
            ftxui::text("status bar")
                | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1),
            ftxui::separator(),
            ftxui::text("input bar")
                | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1),
        });
        ftxui::Render(screen, layout);
        return screen.ToString();
    };

    auto bottom = render();
    REQUIRE(bottom.find("history message 15") != std::string::npos);
    // "history message 1" is a prefix of "history message 11": match with a
    // trailing space so only the actual first message is targeted.
    REQUIRE(bottom.find("history message 1 ") == std::string::npos);
}
