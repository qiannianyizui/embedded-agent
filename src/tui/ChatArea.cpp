// ChatArea — main chat message display with Hermes-style role glyphs and gutter layout
#include "ChatArea.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace ea::tui {

ChatArea::ChatArea() = default;

void ChatArea::append_user(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::User;
    msg.content = text;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_assistant(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::Assistant;
    msg.content = text;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_stream_chunk(const ea::StreamChunk& chunk) {
    if (chunk.type == ea::StreamChunk::Type::Content) {
        streaming_content_ += chunk.data;
        has_new_ = true;
    } else if (chunk.type == ea::StreamChunk::Type::Error) {
        append_error(chunk.data);
    }
}

void ChatArea::append_tool_start(const std::string& name, const std::string& args) {
    ChatMessage msg;
    msg.role = ea::Role::Tool;
    msg.tool_name = name;
    msg.content = args;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_tool_end(const std::string& name, const std::string& result, bool is_error) {
    ChatMessage msg;
    msg.role = ea::Role::Tool;
    msg.tool_name = name;
    msg.content = result;
    msg.is_error = is_error;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_error(const std::string& msg) {
    ChatMessage message;
    message.role = ea::Role::System;
    message.content = msg;
    message.is_error = true;
    message.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(message));
    has_new_ = true;
}

void ChatArea::append_system(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::System;
    msg.content = text;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::append_banner(const std::string& text) {
    ChatMessage msg;
    msg.role = ea::Role::System;
    msg.content = text;
    msg.timestamp = std::chrono::steady_clock::now();
    messages_.push_back(std::move(msg));
    has_new_ = true;
}

void ChatArea::finish_message() {
    if (!streaming_content_.empty()) {
        ChatMessage msg;
        msg.role = ea::Role::Assistant;
        msg.content = std::move(streaming_content_);
        msg.timestamp = std::chrono::steady_clock::now();
        messages_.push_back(std::move(msg));
        streaming_content_.clear();
        has_new_ = true;
    }
}

void ChatArea::clear() {
    messages_.clear();
    streaming_content_.clear();
    has_new_ = false;
    scroll_position_ = 0;
    first_user_message_ = true;
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
// Gutter width: user = prompt glyph width + 1 gap, others = 3
// ---------------------------------------------------------------------------
static int gutter_width(ea::Role role) {
    if (role == ea::Role::User) {
        // "❯ " = 2 display columns (❯ is wide char) + 1 gap = 3
        return 3;
    }
    return 3;  // Fixed 3 for all non-user roles
}

// ---------------------------------------------------------------------------
// Render a single message with Hermes-style glyph + gutter + body
// ---------------------------------------------------------------------------
ftxui::Element ChatArea::render_message(const ChatMessage& msg, int /*width*/) {
    using namespace ftxui;
    auto& theme = default_theme();

    // Error messages — full width, error color
    if (msg.is_error) {
        return hbox({
            text("  · ") | color(theme.color.muted),
            text(msg.content) | color(theme.color.error),
        });
    }

    switch (msg.role) {
        case ea::Role::User: {
            // User: ❯ (label, bold) + text (label)
            // Add separator before 2nd+ user messages
            std::vector<Element> parts;

            if (!first_user_message_) {
                // ─── separator (border color)
                std::string sep;
                for (int i = 0; i < 3; ++i) sep += "─";
                parts.push_back(hbox({
                    text("  ") | color(theme.color.border),
                    text(sep) | color(theme.color.border),
                }));
            }
            first_user_message_ = false;

            parts.push_back(hbox({
                text("❯ ") | color(theme.color.label) | bold,
                text(msg.content) | color(theme.color.label),
            }));

            return vbox(std::move(parts));
        }

        case ea::Role::Assistant: {
            if (msg.streaming) {
                return hbox({
                    text("┊ ") | color(theme.color.border),
                    text(msg.content) | color(theme.color.text),
                    spinner(0, spinner_index_) | color(theme.color.accent),
                });
            }
            // Assistant: ┊ (border) + text (text color)
            // Multi-line content: indent continuation lines
            return hbox({
                text("┊ ") | color(theme.color.border),
                text(msg.content) | color(theme.color.text),
            });
        }

        case ea::Role::Tool: {
            // Tool: ⚡ (muted) + name, wrapped in rounded border
            std::string preview = msg.tool_name;
            if (!msg.content.empty()) {
                // Truncate preview to reasonable length
                const size_t max_preview = 60;
                std::string content_preview = msg.content;
                if (content_preview.size() > max_preview) {
                    content_preview = content_preview.substr(0, max_preview) + "…";
                }
                preview = msg.tool_name + ": " + content_preview;
            }

            auto inner = hbox({
                text("⚡ ") | color(theme.color.muted),
                text(preview) | color(msg.is_error ? theme.color.error : theme.color.muted) | dim,
            });

            return inner | borderRounded | color(theme.color.muted);
        }

        case ea::Role::System:
        default: {
            // System: · (muted) + text (muted)
            return hbox({
                text("· ") | color(theme.color.muted),
                text(msg.content) | color(theme.color.muted) | dim,
            });
        }
    }
}

ftxui::Element ChatArea::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    std::vector<Element> elements;

    // Reset first_user_message_ tracking for this render pass
    first_user_message_ = true;

    for (const auto& msg : messages_) {
        elements.push_back(render_message(msg, 0));
    }

    // If there is active streaming content, render it with a spinner
    if (!streaming_content_.empty()) {
        elements.push_back(hbox({
            text("┊ ") | color(theme.color.border),
            text(streaming_content_) | color(theme.color.text),
            spinner(0, spinner_index_) | color(theme.color.accent),
        }));
    }

    // Advance spinner index for animation
    spinner_index_++;

    if (elements.empty()) {
        elements.push_back(text("") | flex);
    }

    return vbox(std::move(elements)) | flex | frame;
}

}  // namespace ea::tui
