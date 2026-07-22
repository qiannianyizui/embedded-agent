// ChatArea — main chat message display with streaming support
#include "ChatArea.h"
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
        // Error during streaming — append as error message
        append_error(chunk.data);
    }
    // ToolCallBegin, ToolCallDelta, ToolCallEnd, Done are handled
    // by the caller via append_tool_start/end and finish_message
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

ftxui::Element ChatArea::render_message(const ChatMessage& msg, int /*width*/) {
    using namespace ftxui;

    if (msg.is_error) {
        return text(msg.content) | color(Color::Red);
    }

    switch (msg.role) {
        case ea::Role::User:
            return text(msg.content) | color(Color::Cyan) | align_right;

        case ea::Role::Assistant: {
            if (msg.streaming) {
                // Streaming message with spinner
                return hbox({
                    text(msg.content) | color(Color::Green),
                    spinner(0, spinner_index_) | color(Color::Green),
                });
            }
            return text(msg.content) | color(Color::Green);
        }

        case ea::Role::Tool:
            return text("  [" + msg.tool_name + "] " + msg.content)
                | dim | color(Color::GrayDark);

        case ea::Role::System:
        default:
            return text(msg.content) | dim;
    }
}

ftxui::Element ChatArea::render() {
    using namespace ftxui;

    std::vector<Element> elements;

    for (const auto& msg : messages_) {
        elements.push_back(render_message(msg, 0));
    }

    // If there is active streaming content, render it with a spinner
    if (!streaming_content_.empty()) {
        elements.push_back(
            hbox({
                text(streaming_content_) | color(Color::Green),
                spinner(0, spinner_index_) | color(Color::Green),
            })
        );
    }

    // Advance spinner index for animation
    spinner_index_++;

    if (elements.empty()) {
        elements.push_back(text("") | flex);
    }

    return vbox(std::move(elements)) | flex | frame;
}

}  // namespace ea::tui
