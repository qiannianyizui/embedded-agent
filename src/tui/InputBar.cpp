// InputBar — bottom user input area with history navigation and submit callback
#include "InputBar.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

namespace ea::tui {

InputBar::InputBar() {
    ftxui::InputOption option;
    option.placeholder = "Type your message...";
    option.multiline = false;
    option.on_enter = [this] {
        // The on_enter fires when the Input component processes Return.
        // We handle submission in on_event() after the Input has consumed
        // the key, so we just mark it here.
    };

    input_component_ = ftxui::Input(&input_, option);

    // Wrap with CatchEvent to intercept Enter and arrow keys before the
    // Input component sees them.
    auto with_events = input_component_
        | ftxui::CatchEvent(std::function<bool(ftxui::Event)>(
              [this](ftxui::Event event) {
                  return on_event(event);
              }));

    component_ = ftxui::Renderer(with_events, [this] {
        return render();
    });
}

ftxui::Component InputBar::component() {
    return component_;
}

void InputBar::set_busy(bool busy) {
    busy_ = busy;
}

void InputBar::set_on_submit(std::function<void(const std::string&)> fn) {
    on_submit_ = std::move(fn);
}

void InputBar::clear() {
    input_.clear();
    history_index_ = -1;
}

ftxui::Element InputBar::render() {
    using namespace ftxui;

    if (busy_) {
        return text("  ... ") | dim;
    }

    return hbox({
        text("> "),
        input_component_->Render(),
    });
}

bool InputBar::on_event(ftxui::Event event) {
    // When busy, suppress all keyboard input
    if (busy_) {
        return false;
    }

    // Enter key: submit the current input
    if (event == ftxui::Event::Return) {
        if (!input_.empty() && on_submit_) {
            // Add to history (avoid consecutive duplicates)
            if (history_.empty() || history_.back() != input_) {
                history_.push_back(input_);
            }
            on_submit_(input_);
            input_.clear();
            history_index_ = -1;
        }
        return true;  // Consume the event — don't let Input add a newline
    }

    // Up arrow: navigate backward through history when input is empty
    if (event == ftxui::Event::ArrowUp) {
        if (input_.empty() && !history_.empty()) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                history_index_++;
                input_ = history_[history_.size() - 1 - history_index_];
            }
            return true;
        }
    }

    // Down arrow: navigate forward through history when input is empty
    if (event == ftxui::Event::ArrowDown) {
        if (input_.empty() && !history_.empty()) {
            if (history_index_ > 0) {
                history_index_--;
                input_ = history_[history_.size() - 1 - history_index_];
            } else if (history_index_ == 0) {
                history_index_ = -1;
                input_.clear();
            }
            return true;
        }
    }

    return false;  // Let other events pass through to Input
}

}  // namespace ea::tui
