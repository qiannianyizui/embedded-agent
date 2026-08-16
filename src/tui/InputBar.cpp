// InputBar — bottom user input area with mode-aware prompt and hints
#include "InputBar.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>
#include <algorithm>
#include <cctype>
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
    // The input row has an explicit light background; keep the inner text dark.
    option.transform = [](ftxui::InputState state) {
        state.element = state.element
            | ftxui::color(ftxui::Color::RGB(13, 17, 23));
        if (state.is_placeholder) {
            state.element = state.element
                | ftxui::color(ftxui::Color::RGB(91, 100, 116));
        }
        return state.element;
    };

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

void InputBar::set_on_command(std::function<void(const std::string&)> fn) {
    on_command_ = std::move(fn);
}

void InputBar::set_commands(std::vector<CommandEntry> commands) {
    commands_ = std::move(commands);
    selected_ = 0;
}

void InputBar::clear() {
    input_.clear();
    history_index_ = -1;
    selected_ = 0;
    scroll_ = 0;
}

std::vector<int> InputBar::filtered_commands() const {
    std::vector<int> indices;
    if (input_.empty() || input_[0] != '/') return indices;

    std::string query = input_.substr(1);
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (size_t i = 0; i < commands_.size(); ++i) {
        std::string hay = commands_[i].name + " " + commands_[i].description;
        std::transform(hay.begin(), hay.end(), hay.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (query.empty() || hay.find(query) != std::string::npos) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

bool InputBar::suggestions_visible() const {
    return !input_.empty() && input_[0] == '/' && !filtered_commands().empty();
}

int InputBar::command_area_height() const {
    auto indices = filtered_commands();
    if (input_.empty() || input_[0] != '/' || indices.empty()) return 0;
    return std::min(kMaxShown, static_cast<int>(indices.size())) + 2;
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
        prompt = prompt | color(theme.color.bg) | dim;
    } else {
        prompt = prompt | color(theme.color.prompt) | bold;
    }

    Element row = hbox({
               prompt,
               input_component_->Render() | xflex,
               text("  "),
           }) | bgcolor(theme.color.text);

    auto indices = filtered_commands();
    if (input_.empty() || input_[0] != '/' || indices.empty()) {
        return row;
    }
    if (selected_ >= static_cast<int>(indices.size())) selected_ = 0;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ >= static_cast<int>(indices.size())) {
        scroll_ = std::max(0, static_cast<int>(indices.size()) - kMaxShown);
    }

    std::vector<Element> entries;
    int shown = std::min(kMaxShown, static_cast<int>(indices.size()) - scroll_);
    for (int pos = scroll_; pos < scroll_ + shown; ++pos) {
        const auto& entry = commands_[indices[pos]];
        bool selected = pos == selected_;
        Element left = text("  ");
        Element name = text(entry.name) | color(theme.color.text);
        Element desc = text("  " + entry.description)
                           | color(theme.color.muted) | dim;
        if (selected) {
            left = text("▍") | color(theme.color.primary);
            name = text(entry.name) | color(theme.color.primary) | bold;
            desc = text("  " + entry.description) | color(theme.color.muted);
        }
        entries.push_back(hbox({
            left,
            name,
            desc,
            filler(),
        }) | (selected ? bgcolor(theme.color.surface_alt)
                       : bgcolor(theme.color.surface)));
    }

    return vbox({
        row,
        vbox(std::move(entries))
            | borderRounded
            | bgcolor(theme.color.surface)
            | color(theme.color.border),
    });
}

bool InputBar::on_event(ftxui::Event event) {
    auto indices = filtered_commands();
    const bool show = !input_.empty() && input_[0] == '/' && !indices.empty();
    if (show && event.is_mouse()) {
        if (event.mouse().button == ftxui::Mouse::WheelUp && scroll_ > 0) {
            scroll_--;
            return true;
        }
        if (event.mouse().button == ftxui::Mouse::WheelDown &&
            scroll_ + kMaxShown < static_cast<int>(indices.size())) {
            scroll_++;
            return true;
        }
        if (event.mouse().button == ftxui::Mouse::Left &&
            event.mouse().motion == ftxui::Mouse::Pressed) {
            auto dims = ftxui::Terminal::Size();
            int entries_start = dims.dimy - command_area_height() + 1;
            int idx = event.mouse().y - entries_start + scroll_;
            if (idx >= 0 && idx < kMaxShown &&
                idx < static_cast<int>(indices.size())) {
                selected_ = idx;
                return true;
            }
        }
    }
    if (show && event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) {
            selected_--;
            if (selected_ < scroll_) scroll_ = selected_;
        }
        return true;
    }
    if (show && event == ftxui::Event::ArrowDown) {
        if (selected_ + 1 < static_cast<int>(indices.size())) {
            selected_++;
            if (selected_ >= scroll_ + kMaxShown) {
                scroll_ = selected_ - kMaxShown + 1;
            }
        }
        return true;
    }
    if (show && event == ftxui::Event::Tab) {
        input_ = commands_[indices[selected_]].name + " ";
        input_component_->OnEvent(ftxui::Event::End);
        selected_ = 0;
        return true;
    }
    if (show && event == ftxui::Event::Escape) {
        input_.clear();
        selected_ = 0;
        scroll_ = 0;
        return true;
    }
    if (show && event == ftxui::Event::Return) {
        if (on_command_) {
            auto cmd = commands_[indices[selected_]].name;
            input_.clear();
            selected_ = 0;
            scroll_ = 0;
            on_command_(cmd);
        }
        return true;
    }

    // Enter: always submit through the callback. While the agent is busy the
    // TUI queues the message and sends it when the current turn finishes, so
    // pressing Enter during output never silently does nothing.
    if (event == ftxui::Event::Return) {
        if (!input_.empty() && on_submit_) {
            if (history_.empty() || history_.back() != input_) {
                history_.push_back(input_);
            }
            auto submitted = input_;
            input_.clear();
            history_index_ = -1;
            on_submit_(submitted);
        }
        return true;
    }

    if (event.is_character() || event == ftxui::Event::Backspace) {
        selected_ = 0;
        scroll_ = 0;
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
