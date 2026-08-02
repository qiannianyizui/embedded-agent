// SessionSidebar — collapsible session list with active highlight
#include "SessionSidebar.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

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

void SessionSidebar::set_active(const std::string& id) {
    active_id_ = id;
}

void SessionSidebar::set_on_resume(std::function<void(std::string)> fn) {
    on_resume_ = std::move(fn);
}

ftxui::Element SessionSidebar::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    if (!showing_) {
        return text("") | size(WIDTH, EQUAL, 0);
    }

    std::vector<Element> rows;

    // Header with session count
    rows.push_back(hbox({
        text("  SESSIONS") | color(theme.color.muted) | bold,
        filler(),
        text(" " + std::to_string(sessions_.size()) + " ") | color(theme.color.dim) | dim,
        text("  "),
    }));
    rows.push_back(separator());

    if (sessions_.empty()) {
        rows.push_back(text("  No saved sessions") | color(theme.color.muted) | dim);
    } else {
        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
            const auto& session = sessions_[i];
            bool selected = (i == selected_);
            bool active = (session.id == active_id_);

            std::string title = session.title.empty() ? session.id : session.title;
            if (title.size() > 26) title = title.substr(0, 24) + "…";

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

            Element left_rail = text("  ");
            Element title_elem = text(title) | color(theme.color.text);
            Element active_mark = text("   ");

            if (selected) {
                left_rail = text("▍") | color(theme.color.primary);
                title_elem = text(title) | color(theme.color.primary) | bold;
                active_mark = active
                    ? text(" ●") | color(theme.color.ok)
                    : text("   ");
            } else if (active) {
                left_rail = text("▍") | color(theme.color.ok);
                active_mark = text(" ●") | color(theme.color.ok);
            }

            Element row_inner = vbox({
                hbox({ title_elem, filler(), active_mark }),
                hbox({
                    text(meta) | color(theme.color.dim) | dim,
                    filler(),
                }),
            }) | xflex;
            if (selected) {
                row_inner = row_inner | bgcolor(theme.color.surface_alt);
            }
            rows.push_back(hbox({
                left_rail,
                row_inner,
            }));
        }
    }

    rows.push_back(separator());
    rows.push_back(text("  ↑/↓ select  ↵ resume  esc close")
                       | color(theme.color.dim) | dim);

    return vbox(rows)
        | borderRounded | color(theme.color.border)
        | bgcolor(theme.color.surface);
}

bool SessionSidebar::on_event(ftxui::Event event) {
    if (!showing_) {
        if (event.input() == std::string(1, 19)) {  // Ctrl+S
            toggle();
            return true;
        }
        return false;
    }

    if (event.input() == std::string(1, 19)) {
        toggle();
        return true;
    }

    if (event == ftxui::Event::Escape) {
        showing_ = false;
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
        }
        return true;
    }

    return false;
}

}  // namespace ea::tui
