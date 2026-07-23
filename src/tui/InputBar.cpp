// InputBar — bottom user input area with Hermes-style prompt and placeholder
#include "InputBar.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <cstdlib>
#include <ctime>

namespace ea::tui {

namespace {

// Placeholder list — matches Hermes PLACEHOLDERS
const std::vector<std::string> PLACEHOLDERS = {
    "Ask me anything…",
    "Try \"explain this codebase\"",
    "Try \"write a test for…\"",
    "Try \"refactor the auth module\"",
    "Try \"/help\" for commands",
    "Try \"fix the lint errors\"",
    "Try \"how does the config loader work?\"",
};

std::string pick_placeholder() {
    // Simple random pick; seed once
    static bool seeded = false;
    if (!seeded) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        seeded = true;
    }
    return PLACEHOLDERS[std::rand() % PLACEHOLDERS.size()];
}

}  // anonymous namespace

InputBar::InputBar()
    : placeholder_(pick_placeholder()) {
    ftxui::InputOption option;
    option.placeholder = placeholder_;
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
    // Update placeholder based on busy state
    // Note: FTXUI Input doesn't support dynamic placeholder changes easily,
    // so we handle this in render() by showing a different prompt.
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
    auto& theme = default_theme();

    if (busy_) {
        // Busy: muted prompt + dim text
        return hbox({
            text("❯ ") | color(theme.color.muted) | dim,
            text("Ctrl+C to interrupt…") | color(theme.color.muted) | dim,
        });
    }

    // Normal: gold bold ❯ + input
    // Shell mode: if input starts with '!', show blue prompt
    bool shell_mode = !input_.empty() && input_[0] == '!';

    auto prompt_glyph = text("❯ ");
    if (shell_mode) {
        prompt_glyph = prompt_glyph | color(theme.color.shell_dollar);
    } else {
        prompt_glyph = prompt_glyph | color(theme.color.label) | bold;
    }

    return hbox({
        prompt_glyph,
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
