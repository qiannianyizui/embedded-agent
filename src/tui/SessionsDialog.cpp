// SessionsDialog — modal session picker implementation
#include "SessionsDialog.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

namespace ea::tui {

SessionsDialog::SessionsDialog() = default;

ftxui::Component SessionsDialog::component() {
    if (!component_) {
        // The Renderer(bool) overload is Focusable(). Without it the dialog is
        // not focusable, so when it is shown the Modal's internal Container::Tab
        // fails its Focused() check and drops every keyboard event before it
        // reaches on_event — ↑/↓/⏎/Esc would be dead.
        auto renderer = ftxui::Renderer([this](bool) { return render(); });
        component_ = ftxui::CatchEvent(renderer,
            [this](ftxui::Event event) { return on_event(std::move(event)); }
        );
    }
    return component_;
}

void SessionsDialog::set_store(ea::conversation::IConversationStore* store) {
    store_ = store;
}

void SessionsDialog::set_active(const std::string& id) {
    active_id_ = id;
}

void SessionsDialog::set_on_resume(std::function<void(const std::string&)> fn) {
    on_resume_ = std::move(fn);
}

bool SessionsDialog::is_showing() const {
    return showing_;
}

void SessionsDialog::show() {
    refresh();
    showing_ = true;
    selected_ = 0;
}

void SessionsDialog::hide() {
    showing_ = false;
}

void SessionsDialog::refresh() {
    sessions_.clear();
    if (!store_) return;
    auto result = store_->list(50, 0);
    if (result.ok()) {
        sessions_ = std::move(result).value();
    }
}

ftxui::Element SessionsDialog::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    if (!showing_) return text("");
    if (selected_ >= static_cast<int>(sessions_.size())) selected_ = 0;

    std::vector<Element> entries;
    const size_t max_shown = 12;
    size_t start = 0;
    if (static_cast<size_t>(selected_) >= max_shown) {
        start = static_cast<size_t>(selected_) - max_shown + 1;
    }
    size_t shown = std::min(sessions_.size() - start, max_shown);
    for (size_t pos = start; pos < start + shown; ++pos) {
        const auto& session = sessions_[pos];
        bool selected = static_cast<int>(pos) == selected_;
        bool active = session.id == active_id_;

        std::string title = session.title.empty() ? session.id : session.title;
        if (title.size() > 40) title = title.substr(0, 38) + "…";

        std::string meta = std::to_string(session.message_count) + " msgs";
        if (!session.updated_at.empty()) {
            // ISO timestamp "2026-08-03T00:12:34Z" → "08-03 00:12"
            std::string t = session.updated_at;
            std::string compact = t;
            if (t.size() >= 16) {
                compact = t.substr(5, 2) + "-" + t.substr(8, 2)
                        + " " + t.substr(11, 5);
            }
            meta += " · " + compact;
        }
        if (active) {
            meta += " · active";
        }

        Element left = text("  ");
        Element name = text(title) | color(theme.color.text);
        Element desc = text("  " + meta) | color(theme.color.muted) | dim;
        if (selected) {
            left = text("▍") | color(theme.color.primary);
            name = text(title) | color(theme.color.primary) | bold;
            desc = text("  " + meta) | color(theme.color.muted);
        }
        entries.push_back(hbox({
            left,
            vbox({
                hbox({name, filler()}),
                hbox({desc, filler()}),
            }) | xflex
                | (selected ? bgcolor(theme.color.surface_alt)
                            : bgcolor(theme.color.surface)),
        }));
    }

    if (entries.empty()) {
        entries.push_back(text("  No saved sessions")
                              | color(theme.color.muted) | dim);
    }

    std::vector<Element> body;
    body.push_back(hbox({
        text("  Sessions ") | color(theme.color.primary) | bold,
        filler(),
        text(std::to_string(sessions_.size()) + " ") | color(theme.color.dim) | dim,
    }));
    body.push_back(separator());
    body.push_back(vbox(std::move(entries)));
    body.push_back(separator());
    body.push_back(text("  ↵ resume  ·  ↑/↓ select  ·  esc close")
                       | color(theme.color.dim) | dim);

    return vbox(body) | bgcolor(theme.color.surface)
        | borderRounded | color(theme.color.border)
        | size(WIDTH, LESS_THAN, 60);
}

bool SessionsDialog::on_event(ftxui::Event event) {
    if (!showing_) return false;
    if (event == ftxui::Event::Escape) {
        hide();
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) --selected_;
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        if (selected_ < static_cast<int>(sessions_.size()) - 1) ++selected_;
        return true;
    }
    if (event == ftxui::Event::Return) {
        if (selected_ >= 0 && selected_ < static_cast<int>(sessions_.size())) {
            if (on_resume_) {
                on_resume_(sessions_[selected_].id);
            }
            hide();
        }
        return true;
    }
    return false;
}

}  // namespace ea::tui