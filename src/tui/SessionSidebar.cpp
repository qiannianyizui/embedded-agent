// SessionSidebar — collapsible sidebar showing conversation list
#include "SessionSidebar.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>

namespace ea::tui {

SessionSidebar::SessionSidebar(ea::conversation::IConversationStore* store)
    : store_(store) {}

ftxui::Component SessionSidebar::component() {
    if (!component_) {
        auto renderer = ftxui::Renderer([this] { return render(); });
        component_ = ftxui::CatchEvent(renderer,
            [this](ftxui::Event event) { return on_event(std::move(event)); }
        );
    }
    return component_;
}

bool SessionSidebar::is_showing() const {
    return showing_;
}

void SessionSidebar::toggle() {
    showing_ = !showing_;
    if (showing_) {
        refresh();
    }
}

void SessionSidebar::refresh() {
    if (!store_) return;
    auto result = store_->list(50, 0);
    if (result.ok()) {
        sessions_ = std::move(result).value();
        if (selected_ >= static_cast<int>(sessions_.size())) {
            selected_ = 0;
        }
    }
}

void SessionSidebar::set_on_resume(std::function<void(std::string)> fn) {
    on_resume_ = std::move(fn);
}

ftxui::Element SessionSidebar::render() {
    using namespace ftxui;

    if (!showing_) {
        return text("") | size(WIDTH, EQUAL, 0);
    }

    std::vector<Element> entries;
    if (sessions_.empty()) {
        entries.push_back(text("  (no sessions)") | dim);
    } else {
        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
            const auto& session = sessions_[i];
            std::string label = session.title;
            if (label.empty()) {
                label = session.id;
            }
            // Truncate long titles
            if (label.size() > 30) {
                label = label.substr(0, 27) + "...";
            }
            auto line = text("  " + label);
            if (i == selected_) {
                line = line | inverted | focus;
            }
            entries.push_back(std::move(line));
        }
    }

    return vbox({
        text("Sessions") | bold,
        separator(),
        vbox(std::move(entries)) | flex,
    }) | border | size(WIDTH, LESS_THAN, 35);
}

bool SessionSidebar::on_event(ftxui::Event event) {
    if (!showing_) {
        // Ctrl+S toggles sidebar even when hidden
        if (event.input() == std::string(1, 19)) {  // ASCII 19 = Ctrl+S
            toggle();
            return true;
        }
        return false;
    }

    // Ctrl+S toggles sidebar when visible too
    if (event.input() == std::string(1, 19)) {
        toggle();
        return true;
    }

    if (event == ftxui::Event::Escape) {
        showing_ = false;
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) {
            --selected_;
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown) {
        if (selected_ < static_cast<int>(sessions_.size()) - 1) {
            ++selected_;
        }
        return true;
    }

    if (event == ftxui::Event::Return) {
        if (selected_ >= 0 && selected_ < static_cast<int>(sessions_.size())) {
            if (on_resume_) {
                on_resume_(sessions_[selected_].id);
            }
        }
        return true;
    }

    return false;
}

}  // namespace ea::tui
