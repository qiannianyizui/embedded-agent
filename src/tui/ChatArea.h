// ChatArea — main chat message display with Hermes-style role glyphs and gutter layout
#pragma once
#include <ftxui/component/component.hpp>
#include "core/Types.h"
#include <string>
#include <vector>
#include <chrono>

namespace ea::tui {

struct ChatMessage {
    ea::Role role = ea::Role::User;
    std::string content;
    std::string tool_name;    // Only for Tool role
    bool streaming = false;
    bool is_error = false;
    std::chrono::steady_clock::time_point timestamp{};
};

class ChatArea {
public:
    ChatArea();

    // Message append (called from AgentLoop callbacks via Post())
    void append_user(const std::string& text);
    void append_assistant(const std::string& text);
    void append_stream_chunk(const ea::StreamChunk& chunk);
    void append_tool_start(const std::string& name, const std::string& args);
    void append_tool_end(const std::string& name, const std::string& result, bool is_error);
    void append_error(const std::string& msg);
    void append_system(const std::string& text);  // New: system messages
    void append_banner(const std::string& text);  // New: banner message
    void finish_message();  // Streaming output complete
    void clear();

    // FTXUI component
    ftxui::Component component();

    // State queries
    bool has_new_messages() const;
    void clear_new_flag();

    // Test accessors
    const std::vector<ChatMessage>& messages() const { return messages_; }
    const std::string& streaming_content() const { return streaming_content_; }

private:
    ftxui::Component component_;
    std::vector<ChatMessage> messages_;
    std::string streaming_content_;  // Current streaming content buffer
    bool has_new_ = false;
    int scroll_position_ = 0;
    size_t spinner_index_ = 0;
    bool first_user_message_ = true;  // For user message separator logic

    ftxui::Element render_message(const ChatMessage& msg, int width);
    ftxui::Element render();
};

}  // namespace ea::tui
