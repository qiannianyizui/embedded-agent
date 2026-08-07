// CommandPalette — searchable slash-command menu implementation
#include "CommandPalette.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>
#include <cctype>

namespace ea::tui {

CommandPalette::CommandPalette() {
    commands_ = {
        {"/help",   "Show help and keybindings"},
        {"/usage",  "Show token usage"},
        {"/cost",   "Show session cost"},
        {"/history","List conversations"},
        {"/resume", "Resume a conversation"},
        {"/skills", "List available skills"},
        {"/skill <name>", "Load a skill"},
        {"/export", "Export current conversation"},
        {"/import", "Import a conversation"},
        {"/clear",  "Clear current conversation"},
        {"/quit",   "Exit the agent", "⌃C"},
    };
}

ftxui::Component CommandPalette::component() {
    if (!component_) {
        auto renderer = ftxui::Renderer([this] { return render(); });
        component_ = ftxui::CatchEvent(renderer,
            [this](ftxui::Event event) { return on_event(std::move(event)); }
        );
    }
    return component_;
}

bool CommandPalette::is_showing() const {
    return showing_;
}

void CommandPalette::show() {
    showing_ = true;
    filter_.clear();
    selected_ = 0;
}

void CommandPalette::hide() {
    showing_ = false;
    filter_.clear();
}

void CommandPalette::set_on_command(std::function<void(const std::string&)> fn) {
    on_command_ = std::move(fn);
}

std::vector<int> CommandPalette::filtered_indices() const {
    std::vector<int> indices;
    std::string query = filter_;
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (int i = 0; i < static_cast<int>(commands_.size()); ++i) {
        std::string hay = commands_[i].name + " " + commands_[i].description;
        std::transform(hay.begin(), hay.end(), hay.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (query.empty() || hay.find(query) != std::string::npos) {
            indices.push_back(i);
        }
    }
    return indices;
}

ftxui::Element CommandPalette::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    if (!showing_) {
        return text("");
    }

    auto indices = filtered_indices();
    if (selected_ >= static_cast<int>(indices.size())) selected_ = 0;

    std::vector<Element> entries;
    for (size_t pos = 0; pos < indices.size(); ++pos) {
        const auto& entry = commands_[indices[pos]];
        bool selected = (static_cast<int>(pos) == selected_);

        Element left = text("  ");
        Element name = text(entry.name) | color(theme.color.text);
        Element desc = text(entry.description) | color(theme.color.muted) | dim;

        if (selected) {
            left = text("▍") | color(theme.color.primary);
            name = text(entry.name) | color(theme.color.primary) | bold;
            desc = text(entry.description) | color(theme.color.muted);
        }

        Element hint = text("");
        if (!entry.hint.empty()) {
            hint = text("  " + entry.hint) | color(theme.color.dim) | dim;
        }

        entries.push_back(hbox({
            left,
            vbox({
                hbox({ name, filler(), hint }),
                hbox({ desc, filler() }),
            }) | xflex
                | (selected ? bgcolor(theme.color.surface_alt)
                            : bgcolor(theme.color.surface)),
        }));
    }

    if (indices.empty()) {
        entries.push_back(text("  No matching commands")
                              | color(theme.color.muted) | dim);
    }

    std::vector<Element> body;
    body.push_back(hbox({
        text("  Commands ") | color(theme.color.primary) | bold,
        filler(),
        text(std::to_string(indices.size()) + " ") | color(theme.color.dim) | dim,
    }));
    body.push_back(hbox({
        text("  /") | color(theme.color.primary) | bold,
        text(filter_.empty() ? "filter…" : filter_)
            | color(filter_.empty() ? theme.color.dim : theme.color.text),
        filler(),
    }));
    body.push_back(separator());
    body.push_back(vbox(std::move(entries)));
    body.push_back(separator());
    body.push_back(text("  type to filter  ·  ↑/↓ select  ·  ↵ run  ·  esc close")
                       | color(theme.color.dim) | dim);

    return vbox(body) | bgcolor(theme.color.surface)
        | borderRounded | color(theme.color.border)
        | size(WIDTH, LESS_THAN, 58);
}

bool CommandPalette::on_event(ftxui::Event event) {
    if (!showing_) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        if (!filter_.empty()) {
            filter_.clear();
            selected_ = 0;
        } else {
            hide();
        }
        return true;
    }

    if (event == ftxui::Event::Backspace) {
        if (!filter_.empty()) {
            filter_.pop_back();
            selected_ = 0;
        }
        return true;
    }

    if (event == ftxui::Event::Return) {
        auto indices = filtered_indices();
        if (selected_ >= 0 && selected_ < static_cast<int>(indices.size())) {
            if (on_command_) {
                on_command_(commands_[indices[selected_]].name);
            }
        }
        hide();
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) --selected_;
        return true;
    }

    if (event == ftxui::Event::ArrowDown) {
        auto indices = filtered_indices();
        if (selected_ < static_cast<int>(indices.size()) - 1) ++selected_;
        return true;
    }

    // Typing filters the list
    if (event.is_character()) {
        if (event.input().size() == 1) {
            const unsigned char c = static_cast<unsigned char>(event.input()[0]);
            if (c >= 32) {
                filter_ += event.input();
                selected_ = 0;
                return true;
            }
        }
    }

    return false;
}

}  // namespace ea::tui
