// CommandPalette — slash command menu using FTXUI Menu + Modal
#include "CommandPalette.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

namespace ea::tui {

CommandPalette::CommandPalette() {
    commands_ = {
        {"/quit",   "Exit the agent"},
        {"/usage",  "Show token usage"},
        {"/cost",   "Show session cost"},
        {"/history","List conversations"},
        {"/resume", "Resume a conversation"},
        {"/export", "Export current conversation"},
        {"/import", "Import a conversation"},
        {"/clear",  "Clear current conversation"},
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
    selected_ = 0;
}

void CommandPalette::hide() {
    showing_ = false;
}

void CommandPalette::set_on_command(std::function<void(const std::string&)> fn) {
    on_command_ = std::move(fn);
}

ftxui::Element CommandPalette::render() {
    using namespace ftxui;

    if (!showing_) {
        return text("");
    }

    std::vector<Element> entries;
    for (int i = 0; i < static_cast<int>(commands_.size()); ++i) {
        const auto& entry = commands_[i];
        auto line = text(entry.name + " - " + entry.description);
        if (i == selected_) {
            line = line | inverted | focus;
        }
        entries.push_back(std::move(line));
    }

    return vbox({
        text("Commands") | bold,
        separator(),
        vbox(std::move(entries)),
    }) | border | size(WIDTH, LESS_THAN, 50);
}

bool CommandPalette::on_event(ftxui::Event event) {
    if (!showing_) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        hide();
        return true;
    }

    if (event == ftxui::Event::Return) {
        if (selected_ >= 0 && selected_ < static_cast<int>(commands_.size())) {
            if (on_command_) {
                on_command_(commands_[selected_].name);
            }
        }
        hide();
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) {
            --selected_;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown) {
        if (selected_ < static_cast<int>(commands_.size()) - 1) {
            ++selected_;
        }
        return true;
    }

    return false;
}

}  // namespace ea::tui
