// ChatArea — chat transcript with role cards, timestamps and tool blocks
#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include "base/Types.h"
#include <string>
#include <vector>

namespace ea::tui {

enum class ToolStatus {
    Running,
    Ok,
    Error,
};

struct ChatMessage {
    ea::Role role = ea::Role::User;
    std::string content;
    std::string tool_name;        // Only for Tool role
    std::string timestamp;        // Wall-clock "HH:MM" at append time
    ToolStatus tool_status = ToolStatus::Ok;
    int64_t tool_elapsed_ms = 0;  // For Tool role
    bool streaming = false;
    bool is_error = false;
    bool plain = false;   // Render without the role prefix (e.g. welcome card)
};

class ChatArea {
public:
    ChatArea();

    // Message append (called from AgentLoop callbacks via Post())
    void append_user(const std::string& text);
    void append_assistant(const std::string& text);
    void append_stream_chunk(const ea::StreamChunk& chunk);
    void append_tool_start(const std::string& name, const std::string& args);
    void append_tool_end(const std::string& name, const std::string& result,
                         bool is_error, int64_t elapsed_ms = 0);
    void append_error(const std::string& msg);
    void append_system(const std::string& text, bool plain = false);
    // Render persisted history (user/assistant/tool messages) on startup/resume.
    void restore_history(const std::vector<ea::Message>& history);
    void finish_message();
    void clear();

    // FTXUI component
    ftxui::Component component();

    // Handle mouse wheel scrolling over the transcript. Returns true when the
    // event was consumed.
    bool on_event(ftxui::Event event);

    // State queries
    bool has_new_messages() const;
    void clear_new_flag();

    // Test accessors
    const std::vector<ChatMessage>& messages() const { return messages_; }
    const std::string& streaming_content() const { return streaming_content_; }
    float scroll_y() const { return scroll_y_; }
    bool follow_bottom() const { return follow_bottom_; }

private:
    static constexpr float kScrollStep = 0.1f;

    ftxui::Component component_;
    std::vector<ChatMessage> messages_;
    std::string streaming_content_;  // Current streaming content buffer
    bool has_new_ = false;
    size_t spinner_index_ = 0;
    float scroll_y_ = 1.0f;      // 0 = top, 1 = bottom
    bool follow_bottom_ = true;  // Auto-scroll to the newest messages

    ftxui::Element render_user(const ChatMessage& msg);
    ftxui::Element render_assistant(const ChatMessage& msg);
    ftxui::Element render_tool(const ChatMessage& msg);
    ftxui::Element render_system(const ChatMessage& msg);
    ftxui::Element render_message(const ChatMessage& msg);
    ftxui::Element render_streaming();
    ftxui::Element render();
};

}  // namespace ea::tui
