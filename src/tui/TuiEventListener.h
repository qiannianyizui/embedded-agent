// TuiEventListener — bridges AgentEvent to FTXUI UI updates via post_fn
#pragma once
#include "agent/IEventListener.h"
#include <functional>

namespace ea::tui {

class ChatArea;
class StatusBar;

class TuiEventListener : public ea::agent::IEventListener {
public:
    // post_fn wraps ScreenInteractive::Post() — all UI updates must go through it
    TuiEventListener(ChatArea& chat_area, StatusBar& status_bar,
                     std::function<void(std::function<void()>)> post_fn);

    void on_event(const ea::agent::AgentEvent& event) override;

private:
    ChatArea& chat_area_;
    StatusBar& status_bar_;
    std::function<void(std::function<void()>)> post_fn_;
};

}  // namespace ea::tui
