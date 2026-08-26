// InputBar — bottom user input area with mode-aware prompt and hints
#include "InputBar.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/string.hpp>
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

// Cut s to at most max_width display cells, breaking on a UTF-8 glyph
// boundary and appending an ellipsis when content was removed.
std::string fit_to_width(const std::string& s, int max_width) {
    if (max_width <= 0) return "";
    if (ftxui::string_width(s) <= max_width) return s;

    const int body = std::max(0, max_width - 1);
    std::string out;
    int w = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        std::string glyph = s.substr(i, len);
        int gw = ftxui::string_width(glyph);
        if (w + gw > body) break;
        out += glyph;
        w += gw;
        i += len;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "…";
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

void InputBar::set_mode(const std::string& mode) {
    mode_ = mode;
}

void InputBar::set_cwd(const std::string& cwd) {
    cwd_ = cwd;
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

ftxui::Element InputBar::mode_hint() const {
    using namespace ftxui;
    auto& theme = default_theme();

    Element mode;
    if (mode_ == "manual") {
        mode = text("⏵ manual mode") | color(theme.color.text) | bold;
    } else if (mode_ == "acceptEdits") {
        mode = text("⏵⏵ accept edits on") | color(theme.color.warn) | bold;
    } else if (mode_ == "plan") {
        mode = text("⏸ plan mode on") | color(theme.color.accent) | bold;
    } else if (mode_ == "auto" || mode_ == "bypassPermissions") {
        mode = text("⏵⏵⏵ auto mode") | color(theme.color.error) | bold;
    } else {
        return {};
    }

    // The mode is only shown on this bottom line; prefix it with the workspace
    // path so the active project is always visible.
    std::vector<Element> row;
    row.push_back(text("  "));
    if (!cwd_.empty()) {
        row.push_back(text(cwd_) | color(theme.color.muted) | dim);
        row.push_back(text("  ·  ") | color(theme.color.muted) | dim);
    }
    row.push_back(mode);
    return hbox(std::move(row));
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
        return vbox({row, mode_hint()});
    }
    if (selected_ >= static_cast<int>(indices.size())) selected_ = 0;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ >= static_cast<int>(indices.size())) {
        scroll_ = std::max(0, static_cast<int>(indices.size()) - kMaxShown);
    }

    // FTXUI squeezes hbox children proportionally when their combined width
    // overflows, which clips long command names mid-string. Clamp the
    // description ourselves so name + description always fit the terminal.
    const auto dims = ftxui::Terminal::Size();
    const int row_budget = dims.dimx > 5 ? dims.dimx - 5 : 0;

    std::vector<Element> entries;
    int shown = std::min(kMaxShown, static_cast<int>(indices.size()) - scroll_);
    for (int pos = scroll_; pos < scroll_ + shown; ++pos) {
        const auto& entry = commands_[indices[pos]];
        bool selected = pos == selected_;
        std::string desc_str =
            entry.description.empty() ? "" : "  " + entry.description;
        if (ftxui::string_width(entry.name) +
                ftxui::string_width(desc_str) >
            row_budget) {
            desc_str = fit_to_width(
                desc_str, row_budget - static_cast<int>(ftxui::string_width(entry.name)));
        }
        Element left = text("  ");
        Element name = text(entry.name) | color(theme.color.text);
        Element desc = text(desc_str) | color(theme.color.muted) | dim;
        if (selected) {
            left = text("▍") | color(theme.color.primary);
            name = text(entry.name) | color(theme.color.primary) | bold;
            desc = text(desc_str) | color(theme.color.muted);
        }
        entries.push_back(hbox({
            left,
            name,
            desc,
            filler(),
        }) | (selected ? bgcolor(theme.color.surface_alt)
                       : bgcolor(theme.color.surface)));
    }

    std::vector<Element> below;
    below.push_back(mode_hint());
    below.push_back(
        vbox(std::move(entries))
            | borderRounded
            | bgcolor(theme.color.surface)
            | color(theme.color.border));

    return vbox({row, vbox(std::move(below))});
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
