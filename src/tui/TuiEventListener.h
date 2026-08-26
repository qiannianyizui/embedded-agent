// TuiEventListener — bridges AgentEvent to FTXUI UI updates via post_fn
#pragma once
#include "agent/IEventListener.h"
#include <functional>

namespace ea::tui {

class ChatArea;
class StatusBar;
class TopBar;
class InputBar;

class TuiEventListener : public ea::agent::IEventListener {
public:
    // post_fn wraps ScreenInteractive::Post() — all UI updates must go through it
    TuiEventListener(ChatArea& chat_area, StatusBar& status_bar, TopBar& top_bar,
                     InputBar& input_bar,
                     std::function<void(std::function<void()>)> post_fn);

    void on_event(const ea::agent::AgentEvent& event) override;

    // Invoked (posted to the UI thread) when a turn's visible output
    // starts/ends — used to track whether the agent is past its reply or
    // still producing it.
    void set_on_turn_start(std::function<void()> fn) { on_turn_start_ = std::move(fn); }
    void set_on_turn_end(std::function<void()> fn) { on_turn_end_ = std::move(fn); }

private:
    ChatArea& chat_area_;
    StatusBar& status_bar_;
    TopBar& top_bar_;
    InputBar& input_bar_;
    std::function<void(std::function<void()>)> post_fn_;
    std::function<void()> on_turn_start_;
    std::function<void()> on_turn_end_;
};

}  // namespace ea::tui
