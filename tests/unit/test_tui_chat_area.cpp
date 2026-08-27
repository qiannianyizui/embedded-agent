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
    REQUIRE(area.total_lines() == 0);
    REQUIRE(area.scroll_top() == 0);
    REQUIRE(area.follow_bottom());
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

TEST_CASE("ChatArea restore_history renders persisted conversation", "[tui]") {
    ChatArea area;

    std::vector<ea::Message> history;
    history.push_back({ea::Role::User, "Hello", std::nullopt,
                       std::nullopt, std::nullopt});
    history.push_back({ea::Role::Assistant, "Hi there", std::nullopt,
                       std::nullopt, std::nullopt});
    ea::Message tool_msg;
    tool_msg.role = ea::Role::Tool;
    tool_msg.content = "file list ok";
    tool_msg.name = "file";
    history.push_back(std::move(tool_msg));

    area.restore_history(history);

    REQUIRE(area.messages().size() == 3);
    REQUIRE(area.messages()[0].role == ea::Role::User);
    REQUIRE(area.messages()[0].content == "Hello");
    REQUIRE(area.messages()[1].role == ea::Role::Assistant);
    REQUIRE(area.messages()[1].content == "Hi there");
    REQUIRE(area.messages()[2].role == ea::Role::Tool);
    REQUIRE(area.messages()[2].tool_name == "file");
    REQUIRE(area.messages()[2].content == "file list ok");
}

TEST_CASE("ChatArea clear_new_flag idempotent", "[tui]") {
    ChatArea area;
    area.clear_new_flag();
    REQUIRE_FALSE(area.has_new_messages());
    area.clear_new_flag();  // Should not crash
    REQUIRE_FALSE(area.has_new_messages());
}

TEST_CASE("ChatArea total_lines reflects wrap width", "[tui]") {
    ChatArea area;
    area.append_system("aaaa bbbb cccc");

    // No width constraint: single line.
    REQUIRE(area.total_lines() == 1);

    area.set_layout_width(6);
    REQUIRE(area.total_lines() == 3);  // "aaaa" | "bbbb" | "cccc"

    // Width changes re-wrap everything.
    area.set_layout_width(11);
    REQUIRE(area.total_lines() == 2);  // "aaaa bbbb" | "cccc"
}

TEST_CASE("ChatArea total_lines counts rendered message chrome", "[tui]") {
    ChatArea area;
    area.append_user("msg");        // chip + bubble borders + 1 line = 4
    area.append_assistant("reply"); // chip + 1 line = 2
    area.append_tool_start("sh", R"({"cmd":"ls"})");  // collapsed: 1 row
    REQUIRE(area.total_lines() == 7);
}

TEST_CASE("ChatArea tool messages collapse to a single status line", "[tui]") {
    ChatArea area;
    area.set_layout_width(60);
    area.append_tool_start("search_files", R"({"pattern":"auth"})");
    area.append_tool_end("search_files", "12 matches", false, 380);
    REQUIRE(area.total_lines() == 2);  // one row per tool message

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(64),
                                        ftxui::Dimension::Fixed(4));
    ftxui::Render(screen, area.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("search_files") != std::string::npos);
    // Args and result previews stay hidden in the collapsed view.
    REQUIRE(out.find("pattern") == std::string::npos);
    REQUIRE(out.find("12 matches") == std::string::npos);
}

TEST_CASE("ChatArea verbose mode expands tool cards with previews", "[tui]") {
    ChatArea area;
    area.set_layout_width(60);
    area.set_verbose(true);
    area.append_tool_start("search_files", R"({"pattern":"auth"})");
    area.append_tool_end("search_files", "12 matches", false, 380);
    REQUIRE(area.total_lines() == 8);  // title + preview + borders, per message

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(64),
                                        ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, area.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("search_files") != std::string::npos);
    REQUIRE(out.find("12 matches") != std::string::npos);
}

TEST_CASE("ChatArea verbose toggle flips between collapsed and expanded", "[tui]") {
    ChatArea area;
    area.append_tool_start("sh", R"({"cmd":"ls"})");
    area.append_tool_end("sh", "out", false, 0);
    REQUIRE(area.total_lines() == 2);

    area.set_verbose(true);
    REQUIRE(area.verbose());
    REQUIRE(area.total_lines() == 8);

    area.set_verbose(false);
    REQUIRE(area.total_lines() == 2);
}

TEST_CASE("ChatArea hides streaming tool args preview when collapsed", "[tui]") {
    ChatArea area;
    area.set_layout_width(60);
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::ToolCallBegin;
    chunk.tool_call = ea::ToolCall{"1", "patch", R"({"file":"a.cpp"})"};
    area.append_stream_chunk(chunk);
    chunk.data = R"({"file":"a.cpp","edits":[)";
    chunk.type = ea::StreamChunk::Type::ToolCallDelta;
    area.append_stream_chunk(chunk);

    REQUIRE(area.total_lines() == 1);

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(64),
                                        ftxui::Dimension::Fixed(4));
    ftxui::Render(screen, area.component()->Render());
    std::string out = screen.ToString();

    REQUIRE(out.find("patch") != std::string::npos);
    REQUIRE(out.find("edits") == std::string::npos);

    // Expanded, the accumulating preview becomes visible again.
    area.set_verbose(true);
    auto wide = ftxui::Screen::Create(ftxui::Dimension::Fixed(64),
                                      ftxui::Dimension::Fixed(6));
    ftxui::Render(wide, area.component()->Render());
    REQUIRE(wide.ToString().find("edits") != std::string::npos);
}

TEST_CASE("ChatArea wheel scrolls transcript by lines", "[tui]") {
    ChatArea area;
    area.set_viewport_hint(3);
    for (int i = 0; i < 10; ++i) {
        area.append_user("msg " + std::to_string(i));
    }

    REQUIRE(area.total_lines() == 40);  // 4 rows per user message
    REQUIRE(area.scroll_top() == 37);   // 40 - viewport(3)
    REQUIRE(area.follow_bottom());

    REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelUp)));
    REQUIRE(area.scroll_top() == 34);
    REQUIRE_FALSE(area.follow_bottom());

    REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelUp)));
    REQUIRE(area.scroll_top() == 31);

    // Wheel up clamps at the top.
    for (int i = 0; i < 15; ++i) {
        area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    }
    REQUIRE(area.scroll_top() == 0);

    // Scrolling back to the bottom re-enables follow.
    while (area.scroll_top() < 37) {
        REQUIRE(area.on_event(wheel_event(ftxui::Mouse::WheelDown)));
    }
    REQUIRE(area.scroll_top() == 37);
    REQUIRE(area.follow_bottom());
}

TEST_CASE("ChatArea follows bottom on new messages unless scrolled up", "[tui]") {
    ChatArea area;
    area.set_viewport_hint(3);
    for (int i = 0; i < 10; ++i) {
        area.append_user("m" + std::to_string(i));
    }
    REQUIRE(area.scroll_top() == 37);

    area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    area.append_assistant("new message");
    REQUIRE(area.scroll_top() == 34);  // Stay where the user scrolled.
    REQUIRE_FALSE(area.follow_bottom());

    while (area.scroll_top() < 39) {  // 42 rows, viewport 3 -> max 39
        area.on_event(wheel_event(ftxui::Mouse::WheelDown));
    }
    REQUIRE(area.follow_bottom());
    REQUIRE(area.scroll_top() == 39);

    area.append_user("last");
    REQUIRE(area.scroll_top() == 43);  // Follow the newest message again.
}

TEST_CASE("ChatArea viewport follows bottom and scrolls up", "[tui]") {
    ChatArea area;
    area.set_layout_width(56);
    area.set_viewport_hint(8);
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

    while (area.scroll_top() > 0) {
        area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    }

    auto top = render();
    REQUIRE(top.find("message number 0") != std::string::npos);
    REQUIRE(top.find("message number 19") == std::string::npos);
}

TEST_CASE("ChatArea bottom-follows inside full TUI layout", "[tui]") {
    ChatArea area;
    area.set_layout_width(76);
    area.set_viewport_hint(18);  // 24-row screen minus 6 rows of chrome
    for (int i = 0; i < 15; ++i) {
        area.append_system("history message " + std::to_string(i + 1));
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

    // 15 lines fit in the 18-row chat area: everything is visible, pinned
    // at the top, newest message at the bottom.
    auto bottom = render();
    REQUIRE(bottom.find("history message 15") != std::string::npos);
    REQUIRE(bottom.find("history message 1 ") != std::string::npos);
}

TEST_CASE("ChatArea virtualizes long histories", "[tui]") {
    ChatArea area;
    area.set_layout_width(56);
    area.set_viewport_hint(6);
    for (int i = 0; i < 200; ++i) {
        area.append_user("virtual line " + std::to_string(i));
    }

    auto render = [&] {
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                            ftxui::Dimension::Fixed(6));
        ftxui::Render(screen,
                      area.component()->Render() | ftxui::flex | ftxui::yframe);
        return screen.ToString();
    };

    auto bottom = render();
    REQUIRE(bottom.find("virtual line 199") != std::string::npos);
    REQUIRE(bottom.find("virtual line 0") == std::string::npos);
    REQUIRE(area.scroll_top() == 794);  // 800 rows - viewport(6)

    for (int i = 0; i < 30; ++i) {
        area.on_event(wheel_event(ftxui::Mouse::WheelUp));
    }
    REQUIRE(area.scroll_top() == 704);

    auto mid = render();
    REQUIRE(mid.find("virtual line 176") != std::string::npos);
    REQUIRE(mid.find("virtual line 199") == std::string::npos);
    REQUIRE(mid.find("virtual line 0") == std::string::npos);
}

TEST_CASE("ChatArea wraps long CJK lines instead of clipping", "[tui]") {
    ChatArea area;
    area.set_layout_width(26);  // 30-column screen minus 4 margin columns
    area.append_assistant(
        "当然，这完全自愿，你也可以随时再决定，或者直接跳过开始提问就好！😊");

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(30),
                                        ftxui::Dimension::Fixed(8));
    ftxui::Render(screen,
                  area.component()->Render() | ftxui::flex | ftxui::yframe);
    auto out = screen.ToString();

    REQUIRE(out.find("提问就好") != std::string::npos);
}

TEST_CASE("ChatArea wraps streaming output incrementally", "[tui]") {
    ChatArea area;
    area.set_layout_width(10);
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;

    // First word group folds into a final line; the open tail stays partial.
    chunk.data = "aaaa bbbb ";
    area.append_stream_chunk(chunk);
    chunk.data = "cccc";
    area.append_stream_chunk(chunk);
    chunk.data = " dddd";
    area.append_stream_chunk(chunk);

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                        ftxui::Dimension::Fixed(8));
    ftxui::Render(screen,
                  area.component()->Render() | ftxui::flex | ftxui::yframe);
    auto out = screen.ToString();

    // Width 10: "aaaa bbbb" folded, "cccc dddd" open on the next line.
    REQUIRE(out.find("aaaa bbbb") != std::string::npos);
    REQUIRE(out.find("cccc dddd") != std::string::npos);
}

TEST_CASE("ChatArea streaming newlines fold into final lines", "[tui]") {
    ChatArea area;
    ea::StreamChunk chunk;
    chunk.type = ea::StreamChunk::Type::Content;

    chunk.data = "line one\n";
    area.append_stream_chunk(chunk);
    chunk.data = "line two\n";
    area.append_stream_chunk(chunk);
    chunk.data = "line three";
    area.append_stream_chunk(chunk);

    REQUIRE(area.total_lines() == 4);  // 3 lines + chip row

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                        ftxui::Dimension::Fixed(8));
    ftxui::Render(screen,
                  area.component()->Render() | ftxui::flex | ftxui::yframe);
    auto out = screen.ToString();

    REQUIRE(out.find("line one") != std::string::npos);
    REQUIRE(out.find("line two") != std::string::npos);
    REQUIRE(out.find("line three") != std::string::npos);
}
