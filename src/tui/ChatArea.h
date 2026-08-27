// ChatArea — chat transcript with role cards, timestamps and tool blocks
#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include "base/Types.h"
#include <string>
#include <vector>
#include <optional>

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
    bool provisional = false;     // Live tool-call preview (replaced on start)
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

    // Layout hints injected by the enclosing renderer each frame. The wrap
    // width drives line layout; the viewport hint bounds which messages are
    // constructed per frame (virtualization). 0 = disabled.
    void set_layout_width(int width);
    void set_viewport_hint(int height);

    // Verbose mode expands tool messages into full cards (args/result
    // preview); collapsed (default) they render as a single status line.
    void set_verbose(bool verbose);
    bool verbose() const { return verbose_; }

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
    int scroll_top();
    int total_lines();
    bool follow_bottom() const { return follow_bottom_; }

private:
    static constexpr int kScrollStep = 3;  // lines per wheel notch

    ftxui::Component component_;
    std::vector<ChatMessage> messages_;
    std::string streaming_content_;  // Current streaming content buffer
    bool has_new_ = false;
    size_t spinner_index_ = 0;
    int scroll_top_ = 0;             // Scroll offset in wrapped lines (0 = top)
    bool follow_bottom_ = true;      // Auto-scroll to the newest messages
    int layout_width_ = 0;           // 0 = no width constraint
    int viewport_hint_ = 0;          // 0 = no virtualization
    int total_lines_ = 0;            // Wrapped lines (messages + streaming)
    bool verbose_ = false;           // Expanded tool cards vs. one-line status

    // Memoized wrapped lines: wrapped_lines_[i] corresponds to messages_[i].
    // Only rebuilt when the layout width changes or new messages arrive.
    std::vector<std::vector<std::string>> wrapped_lines_;
    int cached_width_ = 0;

    // Streaming wrap state (append-only buffer, wrapped incrementally):
    // streaming_final_ holds lines that can no longer change; the text after
    // the last hard line break is kept unwrapped in streaming_tail_ and
    // re-wrapped per chunk. The last wrapped line stays "open" until the
    // stream ends, because a trailing space may still merge with the next
    // chunk (greedy wrapping is prefix-stable except at trailing spaces).
    std::vector<std::string> streaming_final_;
    std::string streaming_tail_;
    std::string streaming_provisional_;
    std::optional<size_t> tool_preview_index_;    int viewport() const { return viewport_hint_ > 0 ? viewport_hint_ : total_lines_; }
    int max_scroll() const { return std::max(0, total_lines_ - viewport()); }

    void ensure_wrapped();
    void fold_streaming();
    void invalidate_wraps();

    ftxui::Element render_user(const ChatMessage& msg,
                               const std::vector<std::string>& lines);
    ftxui::Element render_assistant(const ChatMessage& msg,
                                    const std::vector<std::string>& lines);
    ftxui::Element render_tool(const ChatMessage& msg);
    ftxui::Element render_system(const ChatMessage& msg,
                                 const std::vector<std::string>& lines);
    ftxui::Element render_message(const ChatMessage& msg,
                                  const std::vector<std::string>& lines);
    ftxui::Element render_streaming();
    ftxui::Element render();
};

}  // namespace ea::tui
