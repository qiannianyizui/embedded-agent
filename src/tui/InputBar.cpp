// InputBar — bottom user input area with mode-aware prompt and hints
#include "InputBar.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <cstdlib>
#include <ctime>

namespace ea::tui {

namespace {

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

    input_component_ = ftxui::Input(&input_, option);

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
    auto& theme = default_theme();

    // Prompt: '❯' (chat) or '$' (shell mode when input starts with '!')
    bool shell_mode = !input_.empty() && input_[0] == '!';
    Element prompt = text("❯ ");
    if (shell_mode) {
        prompt = prompt | color(theme.color.shell) | bold;
    } else if (busy_) {
        // Keep the input visible while the agent works; only dim the prompt.
        prompt = prompt | color(theme.color.muted) | dim;
    } else {
        prompt = prompt | color(theme.color.prompt) | bold;
    }

    // Right-side hint only while empty, so it never fights the cursor
    Element hint = text("Enter to send · /help");
    if (busy_) {
        // Keep the input usable while the agent works; Enter queues the
        // message and it is sent when the current turn finishes.
        hint = text("");
    } else if (!input_.empty()) {
        hint = text("");
    }

    return hbox({
               prompt,
               input_component_->Render() | xflex,
               hint | color(theme.color.dim) | dim,
               text("  "),
           })
        | bgcolor(theme.color.surface);
}

bool InputBar::on_event(ftxui::Event event) {
    // Enter: always submit through the callback. While the agent is busy the
    // TUI queues the message and sends it when the current turn finishes, so
    // pressing Enter during output never silently does nothing.
    if (event == ftxui::Event::Return) {
        if (!input_.empty() && on_submit_) {
            if (history_.empty() || history_.back() != input_) {
                history_.push_back(input_);
            }
            on_submit_(input_);
            input_.clear();
            history_index_ = -1;
        }
        return true;
    }

    // Up/Down: navigate history when the input is empty
    if (event == ftxui::Event::ArrowUp) {
        if (input_.empty() && !history_.empty()) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                history_index_++;
                input_ = history_[history_.size() - 1 - history_index_];
            }
            return true;
        }
    }

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
