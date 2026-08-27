// ChatArea — chat transcript rendering with modern card layout
//
// Performance model: the full conversation is never re-laid out per frame.
// - Message content is wrapped to the layout width once (memoized) and only
//   re-wrapped when the width changes or a message is appended.
// - Streaming output is wrapped incrementally: only the open tail line is
//   re-wrapped per chunk.
// - render() constructs elements only for the message slice that intersects
//   the viewport (virtualization); the rest of the virtual height is padding,
//   so per-frame cost is bounded by the viewport, not the history length.
// - Scrolling is a plain line offset (scroll_top_) plus a focused window box
//   that the surrounding FTXUI frame uses to pin the window to the top of the
//   viewport.
#include "ChatArea.h"
#include "TextWrap.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>
#include <ftxui/dom/take_any_args.hpp>
#include <ftxui/screen/box.hpp>
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
}

void ChatArea::append_assistant(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::Assistant;
    msg.content = text;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_stream_chunk(const ea::StreamChunk& chunk) {
    if (chunk.type == ea::StreamChunk::Type::Content) {
        streaming_content_ += chunk.data;
        streaming_tail_ += chunk.data;
        fold_streaming();
        has_new_ = true;
    } else if (chunk.type == ea::StreamChunk::Type::ToolCallBegin) {
        ChatMessage msg;
        msg.role = ea::Role::Tool;
        msg.tool_name = (chunk.tool_call && !chunk.tool_call->name.empty())
                            ? chunk.tool_call->name : "tool";
        msg.content = (chunk.tool_call && chunk.tool_call->arguments.is_string())
                          ? chunk.tool_call->arguments.get<std::string>() : "";
        msg.tool_status = ToolStatus::Running;
        msg.provisional = true;
        msg.timestamp = fmtClockNow();
        messages_.push_back(std::move(msg));
        tool_preview_index_ = messages_.size() - 1;
        has_new_ = true;
    } else if (chunk.type == ea::StreamChunk::Type::ToolCallDelta) {
        if (tool_preview_index_ && *tool_preview_index_ < messages_.size()) {
            auto& msg = messages_[*tool_preview_index_];
            if (chunk.tool_call && !chunk.tool_call->name.empty()) {
                msg.tool_name = chunk.tool_call->name;
            }
            msg.content += chunk.data;
            has_new_ = true;
        }
    } else if (chunk.type == ea::StreamChunk::Type::ToolCallEnd) {
        // Preview is complete — no longer provisional; execution may follow.
        tool_preview_index_.reset();
        has_new_ = true;
    } else if (chunk.type == ea::StreamChunk::Type::Error) {
        append_error(chunk.data);
    }
}

void ChatArea::append_tool_start(const std::string& name, const std::string& args) {
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->role == ea::Role::Tool && it->provisional &&
            (it->tool_name == name || it->tool_name == "tool")) {
            it->tool_name = name;
            it->content = args;
            it->provisional = false;
            it->tool_status = ToolStatus::Running;
            it->timestamp = fmtClockNow();
            has_new_ = true;
            tool_preview_index_.reset();
            return;
        }
    }
    ChatMessage msg;
    msg.role = ea::Role::Tool;
    msg.tool_name = name;
    msg.content = args;
    msg.tool_status = ToolStatus::Running;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_tool_end(const std::string& name, const std::string& result,
                               bool is_error, int64_t elapsed_ms) {
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->role == ea::Role::Tool && it->provisional &&
            (it->tool_name == name || it->tool_name == "tool")) {
            it->tool_name = name;
            it->content = result;
            it->provisional = false;
            it->is_error = is_error;
            it->tool_status = is_error ? ToolStatus::Error : ToolStatus::Ok;
            it->tool_elapsed_ms = elapsed_ms;
            has_new_ = true;
            tool_preview_index_.reset();
            return;
        }
    }
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
}

void ChatArea::append_error(const std::string& msg) {
    ChatMessage message;
    message.role = ea::Role::System;
    message.content = msg;
    message.is_error = true;
    message.timestamp = fmtClockNow();
    messages_.push_back(std::move(message));
    has_new_ = true;
}

void ChatArea::append_system(const std::string& text, bool plain) {
    ChatMessage msg;
    msg.role = ea::Role::System;
    msg.content = text;
    msg.plain = plain;
    msg.timestamp = fmtClockNow();
    messages_.push_back(std::move(msg));
    has_new_ = true;
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
        streaming_final_.clear();
        streaming_tail_.clear();
        streaming_provisional_.clear();
        has_new_ = true;
    }
}

void ChatArea::clear() {
    messages_.clear();
    wrapped_lines_.clear();
    streaming_content_.clear();
    streaming_final_.clear();
    streaming_tail_.clear();
    streaming_provisional_.clear();
    tool_preview_index_.reset();
    has_new_ = false;
    total_lines_ = 0;
    scroll_top_ = 0;
    follow_bottom_ = true;
}

void ChatArea::set_layout_width(int width) {
    layout_width_ = width > 0 ? width : 0;
}

void ChatArea::set_viewport_hint(int height) {
    viewport_hint_ = height > 0 ? height : 0;
}

void ChatArea::set_verbose(bool verbose) {
    verbose_ = verbose;
}

bool ChatArea::on_event(ftxui::Event event) {
    if (!event.is_mouse()) return false;
    if (event.mouse().button == ftxui::Mouse::WheelUp) {
        ensure_wrapped();
        if (follow_bottom_) scroll_top_ = max_scroll();
        if (scroll_top_ > 0) {
            scroll_top_ = std::max(0, scroll_top_ - kScrollStep);
            follow_bottom_ = false;
        }
        return true;
    }
    if (event.mouse().button == ftxui::Mouse::WheelDown) {
        ensure_wrapped();
        if (follow_bottom_) scroll_top_ = max_scroll();
        const int bottom = max_scroll();
        if (scroll_top_ < bottom) {
            scroll_top_ = std::min(bottom, scroll_top_ + kScrollStep);
            if (scroll_top_ >= bottom) follow_bottom_ = true;
        }
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

int ChatArea::total_lines() {
    ensure_wrapped();
    return total_lines_;
}

int ChatArea::scroll_top() {
    ensure_wrapped();
    if (follow_bottom_) scroll_top_ = max_scroll();
    return scroll_top_;
}

// ---------------------------------------------------------------------------
// Virtual height model
// ---------------------------------------------------------------------------
// The virtual line space mirrors the rendered rows exactly:
//   user:      chip(1) + bubble borders(2) + wrapped lines
//   assistant: chip(1) + wrapped lines
//   tool:      verbose: title(1) + body(1) + borders(2); collapsed: 1
//   system:    wrapped lines (no chrome)
//   streaming: chip(1) + wrapped lines
// Keeping these in lockstep with render_*() guarantees window slicing never
// shows gaps or overlaps.

namespace {

int message_height(const ChatMessage& msg,
                   const std::vector<std::string>& lines,
                   bool verbose) {
    const int n = static_cast<int>(lines.size());
    switch (msg.role) {
        case ea::Role::User:
            return n + 3;
        case ea::Role::Assistant:
            return msg.is_error ? n : n + 1;
        case ea::Role::Tool:
            return verbose ? 4 : 1;
        case ea::Role::System:
        default:
            return n;
    }
}

int streaming_height(const std::vector<std::string>& final_lines,
                     const std::string& provisional) {
    int n = static_cast<int>(final_lines.size());
    if (!provisional.empty()) ++n;
    if (n == 0) return 0;
    return n + 1;  // chip row
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Wrap cache management
// ---------------------------------------------------------------------------

void ChatArea::fold_streaming() {
    if (streaming_tail_.empty()) return;

    auto lines = wrap_by_width(streaming_tail_, layout_width_);
    const bool tail_ends_newline = streaming_tail_.back() == '\n';
    if (tail_ends_newline) {
        // Every wrapped line is final (hard break at the tail's newline).
        for (auto& line : lines) streaming_final_.push_back(std::move(line));
        streaming_provisional_.clear();
    } else {
        // All but the last line are final; the last may still merge with the
        // next chunk (a trailing space could absorb a following word).
        for (size_t i = 0; i + 1 < lines.size(); ++i) {
            streaming_final_.push_back(std::move(lines[i]));
        }
        streaming_provisional_ =
            lines.empty() ? std::string() : std::move(lines.back());
    }

    // The wrapped prefix before the last hard break can never change: drop it
    // from the tail so per-chunk re-wrapping stays O(open line), not O(stream).
    size_t nl = streaming_tail_.find_last_of('\n');
    if (nl != std::string::npos) {
        streaming_tail_.erase(0, nl + 1);
    }
}

void ChatArea::ensure_wrapped() {
    if (layout_width_ != cached_width_) {
        // Layout width changed (terminal resize): re-wrap everything.
        wrapped_lines_.resize(messages_.size());
        for (size_t i = 0; i < messages_.size(); ++i) {
            wrapped_lines_[i] = wrap_by_width(messages_[i].content, layout_width_);
        }
        streaming_final_.clear();
        streaming_provisional_.clear();
        streaming_tail_ = streaming_content_;
        fold_streaming();
        cached_width_ = layout_width_;
    } else if (wrapped_lines_.size() < messages_.size()) {
        // New messages appended: wrap only the tail.
        const size_t from = wrapped_lines_.size();
        wrapped_lines_.resize(messages_.size());
        for (size_t i = from; i < messages_.size(); ++i) {
            wrapped_lines_[i] = wrap_by_width(messages_[i].content, layout_width_);
        }
    }

    int total = 0;
    for (size_t i = 0; i < messages_.size(); ++i) {
        total += message_height(messages_[i], wrapped_lines_[i], verbose_);
    }
    total += streaming_height(streaming_final_, streaming_provisional_);
    total_lines_ = total;
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

namespace {

// Focus the given row range (in virtual lines) so a surrounding FTXUI frame
// scrolls that window to the top of the viewport. The frame centers the
// focused box, so the focused box must span the whole viewport window.
class WindowFocus : public ftxui::Node {
public:
    WindowFocus(ftxui::Element child, int y_min, int y_max)
        : Node(ftxui::unpack(std::move(child))), y_min_(y_min), y_max_(y_max) {}

    void ComputeRequirement() override {
        if (children_.empty()) return;
        children_[0]->ComputeRequirement();
        requirement_ = children_[0]->requirement();
        const int last = std::max(0, requirement_.min_y - 1);
        requirement_.focused.enabled = true;
        requirement_.focused.node = this;
        requirement_.focused.box.x_min = 0;
        requirement_.focused.box.x_max = std::max(0, requirement_.min_x - 1);
        requirement_.focused.box.y_min = std::min(y_min_, last);
        requirement_.focused.box.y_max = std::min(y_max_, last);
    }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        if (!children_.empty()) children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        if (!children_.empty()) children_[0]->Render(screen);
    }

private:
    int y_min_;
    int y_max_;
};

ftxui::Element text_block(const std::vector<std::string>& lines) {
    using namespace ftxui;
    std::vector<Element> elems;
    elems.reserve(lines.size());
    for (const auto& line : lines) {
        elems.push_back(text(line));
    }
    return vbox(std::move(elems));
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
ftxui::Element ChatArea::render_user(const ChatMessage& msg,
                                     const std::vector<std::string>& lines) {
    using namespace ftxui;
    auto& theme = default_theme();
    auto role = theme.role_user();

    auto bubble = hbox({
        text("  "),
        text_block(lines) | color(role.body_color) | xflex,
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
ftxui::Element ChatArea::render_assistant(const ChatMessage& msg,
                                          const std::vector<std::string>& lines) {
    using namespace ftxui;
    auto& theme = default_theme();
    auto role = theme.role_assistant();

    return hbox({
        text("┃ ") | color(role.accent_color) | bold,
        vbox({
            render_chip(role.label, msg.timestamp, role.chip_color),
            text_block(lines) | color(role.body_color) | xflex,
        }) | xflex,
    });
}

// Tool message — bordered card with status icon and elapsed time. Collapsed
// (default) it shrinks to a single status line that hides the args/result
// preview: tool chatter is the agent's internal monologue, expanded on demand
// via Ctrl+O (set_verbose).
ftxui::Element ChatArea::render_tool(const ChatMessage& msg) {
    using namespace ftxui;
    auto& theme = default_theme();

    Color status_color = theme.color.muted;
    std::string status_glyph = "▶";
    std::string status_label = "running";
    if (msg.provisional) {
        status_color = theme.color.accent;
        status_glyph = "⧗";
        status_label = "generating";
    } else if (msg.tool_status == ToolStatus::Ok) {
        status_color = theme.color.ok;
        status_glyph = "✓";
        status_label = "done";
    } else if (msg.tool_status == ToolStatus::Error) {
        status_color = theme.color.error;
        status_glyph = "✗";
        status_label = "error";
    }

    std::string elapsed =
        msg.tool_elapsed_ms > 0 ? fmtElapsed(msg.tool_elapsed_ms) + " · " : "";

    if (!verbose_) {
        return hbox({
            text("  " + status_glyph + " " + msg.tool_name + " ")
                | color(status_color) | (msg.is_error ? bold : dim),
            filler(),
            text(" " + elapsed + status_label + "  ") | color(status_color) | dim,
        });
    }

    std::string preview = truncate(first_line(msg.content), 96);

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
ftxui::Element ChatArea::render_system(const ChatMessage& msg,
                                       const std::vector<std::string>& lines) {
    using namespace ftxui;
    auto& theme = default_theme();

    Color c = msg.is_error ? theme.color.error : theme.color.muted;
    if (msg.plain) {
        return hbox({
            text("  "),
            text_block(lines) | color(c) | dim | xflex,
        });
    }
    std::string glyph = msg.is_error ? "⚠" : "·";

    return hbox({
        text("  " + glyph + " ") | color(c),
        text_block(lines) | color(c) | (msg.is_error ? ftxui::bold : ftxui::dim) | xflex,
    });
}

ftxui::Element ChatArea::render_message(const ChatMessage& msg,
                                        const std::vector<std::string>& lines) {
    switch (msg.role) {
        case ea::Role::User:
            return render_user(msg, lines);
        case ea::Role::Assistant:
            if (msg.is_error) return render_system(msg, lines);
            return render_assistant(msg, lines);
        case ea::Role::Tool:
            return render_tool(msg);
        case ea::Role::System:
        default:
            return render_system(msg, lines);
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

    std::vector<Element> text_lines;
    for (const auto& line : streaming_final_) {
        text_lines.push_back(text(line));
    }
    if (!streaming_provisional_.empty()) {
        text_lines.push_back(text(streaming_provisional_));
    }
    if (text_lines.empty()) text_lines.push_back(text(""));

    return hbox({
        text("┃ ") | color(role.accent_color) | bold,
        vbox({
            render_chip(role.label, "", role.chip_color),
            hbox({
                text(frame + " ") | color(theme.color.accent),
                vbox(std::move(text_lines)) | color(role.body_color) | xflex,
            }),
        }) | xflex,
    });
}

ftxui::Element ChatArea::render() {
    using namespace ftxui;

    ensure_wrapped();
    if (follow_bottom_) {
        scroll_top_ = max_scroll();
    } else {
        scroll_top_ = std::clamp(scroll_top_, 0, max_scroll());
    }

    if (total_lines_ == 0) {
        return hbox({
            text("  "),
            text("") | flex,
            text("  "),
        });
    }

    // Window slice: [window_top, window_bottom) of the virtual line space.
    const int vp = std::max(1, viewport());
    const int window_top = scroll_top_;
    const int window_bottom = std::min(total_lines_, scroll_top_ + vp);

    std::vector<Element> elements;
    int line = 0;
    int first_row = window_top;  // virtual row of the first built element
    bool built_any = false;
    for (size_t i = 0; i < messages_.size() && line < window_bottom; ++i) {
        const auto& lines = wrapped_lines_[i];
        const int count = message_height(messages_[i], lines, verbose_);
        if (line + count > window_top) {
            if (!built_any) {
                first_row = line;
                built_any = true;
            }
            elements.push_back(render_message(messages_[i], lines));
        }
        line += count;
    }
    if (!streaming_content_.empty()) {
        const int count =
            streaming_height(streaming_final_, streaming_provisional_);
        if (line + count > window_top) {
            if (!built_any) {
                first_row = line;
                built_any = true;
            }
            elements.push_back(render_streaming());
        }
        line += count;
    }
    spinner_index_++;

    if (elements.empty()) {
        elements.push_back(text("") | flex);
    }

    Element content;
    if (scroll_top_ > 0) {
        // Messages are windowed at message granularity, so the window starts
        // at |first_row| (<= scroll_top_). Pad above with exactly that offset
        // and focus the window in child coordinates; the surrounding frame
        // then pins virtual row scroll_top_ to the top of the viewport.
        content = vbox({
            text("") | size(HEIGHT, EQUAL, first_row),
            vbox(std::move(elements)) | xflex,
            text("") | size(HEIGHT, EQUAL, std::max(0, total_lines_ - line)),
        }) | xflex;
        const int focus_ymin = scroll_top_;
        const int focus_ymax = std::min(scroll_top_ + vp - 1,
                                        std::max(scroll_top_, total_lines_ - 1));
        content = std::make_shared<WindowFocus>(std::move(content),
                                                focus_ymin, focus_ymax);
    } else {
        content = vbox(std::move(elements)) | xflex;
    }

    return hbox({
        text("  "),
        content,
        text("  "),
    });
}

}  // namespace ea::tui
