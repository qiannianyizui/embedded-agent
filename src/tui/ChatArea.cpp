// ChatArea — chat transcript rendering with modern card layout
#include "ChatArea.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <algorithm>
#include <cctype>

namespace ea::tui {

ChatArea::ChatArea() = default;

void ChatArea::append_user(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::User;
    msg.content = text;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::append_assistant(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::Assistant;
    msg.content = text;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::append_stream_chunk(const ea::StreamChunk& chunk) {
    if (chunk.type == ea::StreamChunk::Type::Content) {
        streaming_content_ += chunk.data;
        has_new_ = true;
        if (follow_bottom_) scroll_y_ = 1.0f;
    } else if (chunk.type == ea::StreamChunk::Type::Error) {
        append_error(chunk.data);
    }
}

void ChatArea::append_tool_start(const std::string& name, const std::string& args) {
    ChatMessage msg;
    msg.role = ea::Role::Tool;
    msg.tool_name = name;
    msg.content = args;
    msg.tool_status = ToolStatus::Running;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::append_tool_end(const std::string& name, const std::string& result,
                               bool is_error, int64_t elapsed_ms) {
    ChatMessage msg;
    msg.role = ea::Role::Tool;
    msg.tool_name = name;
    msg.content = result;
    msg.is_error = is_error;
    msg.tool_status = is_error ? ToolStatus::Error : ToolStatus::Ok;
    msg.tool_elapsed_ms = elapsed_ms;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::append_error(const std::string& msg) {
    ChatMessage message;
    message.role = ea::Role::System;
    message.content = msg;
    message.is_error = true;
    message.timestamp = fmtClockNow();
    messages_.push_back(std::move(message));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::append_system(const std::string& text, bool plain) {
    ChatMessage msg;
    msg.role = ea::Role::System;
    msg.content = text;
    msg.plain = plain;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
    if (follow_bottom_) scroll_y_ = 1.0f;
}

void ChatArea::restore_history(const std::vector<ea::Message>& history) {
    for (const auto& msg : history) {
        if (msg.content.empty()) continue;
        switch (msg.role) {
            case ea::Role::User:
                append_user(msg.content);
                break;
            case ea::Role::Assistant:
                append_assistant(msg.content);
                break;
            case ea::Role::Tool:
                append_tool_end(msg.name ? *msg.name : "tool", msg.content,
                                /*is_error=*/false);
                break;
            case ea::Role::System:
                append_system(msg.content);
                break;
        }
    }
}

void ChatArea::finish_message() {
    if (!streaming_content_.empty()) {
        ChatMessage msg;
        msg.role = ea::Role::Assistant;
        msg.content = std::move(streaming_content_);
        msg.timestamp = fmtClockNow();
        messages_.push_back(std::move(msg));
        streaming_content_.clear();
        has_new_ = true;
        if (follow_bottom_) scroll_y_ = 1.0f;
    }
}

void ChatArea::clear() {
    messages_.clear();
    streaming_content_.clear();
    has_new_ = false;
    scroll_y_ = 1.0f;
    follow_bottom_ = true;
}

bool ChatArea::on_event(ftxui::Event event) {
    if (!event.is_mouse()) return false;
    if (event.mouse().button == ftxui::Mouse::WheelUp) {
        follow_bottom_ = false;
        scroll_y_ = std::max(0.0f, scroll_y_ - kScrollStep);
        return true;
    }
    if (event.mouse().button == ftxui::Mouse::WheelDown) {
        scroll_y_ = std::min(1.0f, scroll_y_ + kScrollStep);
        if (scroll_y_ >= 1.0f) follow_bottom_ = true;
        return true;
    }
    return false;
}

ftxui::Component ChatArea::component() {
    if (!component_) {
        component_ = ftxui::Renderer([this] { return render(); });
    }
    return component_;
}

bool ChatArea::has_new_messages() const {
    return has_new_;
}

void ChatArea::clear_new_flag() {
    has_new_ = false;
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

namespace {

// Word-wrapping renderer that also breaks between CJK characters (FTXUI's
// paragraph() only wraps on ASCII spaces, so Chinese long lines get clipped).
ftxui::Element wrap_text(const std::string& content) {
    using namespace ftxui;
    Elements lines;
    size_t start = 0;
    while (start <= content.size()) {
        size_t nl = content.find('\n', start);
        std::string line = (nl == std::string::npos)
            ? content.substr(start) : content.substr(start, nl - start);

        Elements tokens;
        std::string word;
        auto flush_word = [&] {
            if (!word.empty()) {
                tokens.push_back(text(word));
                word.clear();
            }
        };

        for (size_t i = 0; i < line.size();) {
            unsigned char c = static_cast<unsigned char>(line[i]);
            if (c == ' ') {
                flush_word();
                tokens.push_back(text(" "));
                ++i;
            } else if (c < 0x80) {
                if (std::isalnum(c) || c == '_' || c == '-' || c == '.' ||
                    c == '/') {
                    word += static_cast<char>(c);
                } else {
                    flush_word();
                    tokens.push_back(text(std::string(1, static_cast<char>(c))));
                }
                ++i;
            } else {
                flush_word();
                size_t len = 1;
                if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                tokens.push_back(text(line.substr(i, len)));
                i += len;
            }
        }
        flush_word();

        if (tokens.empty()) tokens.push_back(text(""));
        lines.push_back(flexbox(std::move(tokens), FlexboxConfig().SetGap(0, 0)));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return vbox(std::move(lines));
}

// Small role chip: "● agent · 12:03"
ftxui::Element render_chip(const std::string& label, const std::string& time,
                           ftxui::Color col) {
    using namespace ftxui;
    std::vector<Element> parts;
    parts.push_back(text("● ") | color(col));
    parts.push_back(text(label) | color(col) | bold);
    if (!time.empty()) {
        parts.push_back(text("  " + time) | color(col) | dim);
    }
    return hbox(std::move(parts));
}

// First line of a (possibly multiline) string
std::string first_line(const std::string& s) {
    auto pos = s.find('\n');
    if (pos == std::string::npos) return s;
    return s.substr(0, pos);
}

// Truncate a preview to max_len characters
std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "…";
}

}  // anonymous namespace

// User message — right-aligned indigo bubble with chip above
ftxui::Element ChatArea::render_user(const ChatMessage& msg) {
    using namespace ftxui;
    auto& theme = default_theme();
    auto role = theme.role_user();

    auto bubble = hbox({
        text("  "),
        wrap_text(msg.content) | color(role.body_color) | xflex,
        text("  "),
    }) | bgcolor(theme.color.user_bg)
       | borderRounded | color(theme.color.rail);

    return hbox({
        filler(),
        vbox({
            hbox({ filler(), render_chip(role.label, msg.timestamp, role.chip_color) }),
            bubble,
        }),
    });
}

// Assistant message — left rail + chip + body
ftxui::Element ChatArea::render_assistant(const ChatMessage& msg) {
    using namespace ftxui;
    auto& theme = default_theme();
    auto role = theme.role_assistant();

    return hbox({
        text("┃ ") | color(role.accent_color) | bold,
        vbox({
            render_chip(role.label, msg.timestamp, role.chip_color),
            wrap_text(msg.content) | color(role.body_color) | xflex,
        }) | xflex,
    });
}

// Tool message — bordered card with status icon and elapsed time
ftxui::Element ChatArea::render_tool(const ChatMessage& msg) {
    using namespace ftxui;
    auto& theme = default_theme();

    Color status_color = theme.color.muted;
    std::string status_glyph = "▶";
    std::string status_label = "running";
    if (msg.tool_status == ToolStatus::Ok) {
        status_color = theme.color.ok;
        status_glyph = "✓";
        status_label = "done";
    } else if (msg.tool_status == ToolStatus::Error) {
        status_color = theme.color.error;
        status_glyph = "✗";
        status_label = "error";
    }

    std::string preview = truncate(first_line(msg.content), 96);
    std::string elapsed =
        msg.tool_elapsed_ms > 0 ? fmtElapsed(msg.tool_elapsed_ms) + " · " : "";

    auto title_row = hbox({
        text("  " + status_glyph + " " + msg.tool_name + " ") | color(status_color) | bold,
        filler(),
        text(" " + elapsed + status_label + " ") | color(status_color) | dim,
        text("  "),
    });
    auto body_row = hbox({
        text("  ") | color(theme.color.muted),
        text(preview) | color(theme.color.muted),
        filler(),
        text("  "),
    });

    return vbox({title_row, body_row}) | bgcolor(theme.color.tool_bg)
        | borderRounded | color(theme.color.border);
}

// System message — centered, muted; errors in red
ftxui::Element ChatArea::render_system(const ChatMessage& msg) {
    using namespace ftxui;
    auto& theme = default_theme();

    Color c = msg.is_error ? theme.color.error : theme.color.muted;
    if (msg.plain) {
        return hbox({
            text("  "),
            wrap_text(msg.content) | color(c) | dim | xflex,
        });
    }
    std::string glyph = msg.is_error ? "⚠" : "·";

    return hbox({
        text("  " + glyph + " ") | color(c),
        wrap_text(msg.content) | color(c) | (msg.is_error ? ftxui::bold : ftxui::dim) | xflex,
    });
}

ftxui::Element ChatArea::render_message(const ChatMessage& msg) {
    switch (msg.role) {
        case ea::Role::User:
            return render_user(msg);
        case ea::Role::Assistant:
            if (msg.is_error) return render_system(msg);
            return render_assistant(msg);
        case ea::Role::Tool:
            return render_tool(msg);
        case ea::Role::System:
        default:
            return render_system(msg);
    }
}

// Streaming pseudo-message with animated braille spinner
ftxui::Element ChatArea::render_streaming() {
    using namespace ftxui;
    auto& theme = default_theme();
    auto role = theme.role_assistant();

    static const std::vector<std::string> frames = {
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏",
    };
    std::string frame = frames[spinner_index_ % frames.size()];

    return hbox({
        text("┃ ") | color(role.accent_color) | bold,
        vbox({
            render_chip(role.label, "", role.chip_color),
            hbox({
                text(frame + " ") | color(theme.color.accent),
                wrap_text(streaming_content_) | color(role.body_color) | xflex,
            }),
        }) | xflex,
    });
}

ftxui::Element ChatArea::render() {
    using namespace ftxui;

    std::vector<Element> elements;
    for (const auto& msg : messages_) {
        elements.push_back(render_message(msg));
    }
    if (!streaming_content_.empty()) {
        elements.push_back(render_streaming());
    }

    spinner_index_++;

    if (elements.empty()) {
        elements.push_back(text("") | flex);
    }

    // Left/right page margins for a calmer reading column
    return hbox({
               text("  "),
               vbox(std::move(elements)) | xflex,
               text("  "),
           })
        | focusPositionRelative(0.f, scroll_y_);
}

}  // namespace ea::tui
