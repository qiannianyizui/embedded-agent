// SkillsDialog — modal skill manager implementation
#include "SkillsDialog.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

namespace ea::tui {

SkillsDialog::SkillsDialog() = default;

ftxui::Component SkillsDialog::component() {
    if (!component_) {
        // The Renderer(bool) overload is Focusable(). Without it the dialog is
        // not focusable, so when it is shown the Modal's internal Container::Tab
        // fails its Focused() check and drops every keyboard event before it
        // reaches on_event — ↑/↓/⏎/Esc would be dead.
        auto renderer = ftxui::Renderer([this](bool) { return render(); });
        component_ = ftxui::CatchEvent(renderer,
            [this](ftxui::Event event) { return on_event(std::move(event)); });
    }
    return component_;
}

void SkillsDialog::set_manager(ea::skill::SkillManager* manager) {
    skills_ = manager;
}

void SkillsDialog::set_on_changed(std::function<void()> fn) {
    on_changed_ = std::move(fn);
}

bool SkillsDialog::is_showing() const {
    return showing_;
}

void SkillsDialog::show() {
    refresh();
    showing_ = true;
    selected_ = 0;
}

void SkillsDialog::hide() {
    showing_ = false;
}

void SkillsDialog::refresh() {
    entries_.clear();
    if (!skills_) return;
    auto all = skills_->list_all();
    if (all.ok()) {
        entries_ = std::move(all.value());
    }
}

ftxui::Element SkillsDialog::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    if (!showing_) return text("");
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = 0;

    std::vector<Element> entries;
    const size_t max_shown = 14;
    size_t start = 0;
    if (static_cast<size_t>(selected_) >= max_shown) {
        start = static_cast<size_t>(selected_) - max_shown + 1;
    }
    size_t shown = std::min(entries_.size() - start, max_shown);
    for (size_t pos = start; pos < start + shown; ++pos) {
        const auto& info = entries_[pos];
        bool selected = static_cast<int>(pos) == selected_;
        std::string display = info.alias.empty() ? info.name : info.alias;
        Element checkbox = text(info.enabled ? "[x]" : "[ ]");
        Element name = text(display) | color(info.enabled
                                                 ? theme.color.text
                                                 : theme.color.muted) | dim;
        Element desc = text(info.description) | color(theme.color.muted) | dim;

        if (selected) {
            checkbox = checkbox | color(theme.color.primary);
            name = text(display) | color(theme.color.primary) | bold;
            desc = text(info.description) | color(theme.color.muted);
        }
        entries.push_back(hbox({
            text("  ") | color(theme.color.muted),
            checkbox | color(theme.color.muted),
            text(" "),
            vbox({
                hbox({name, filler()}),
                hbox({desc, filler()}),
            }) | xflex
                | (selected ? bgcolor(theme.color.surface_alt)
                            : bgcolor(theme.color.surface)),
        }));
    }

    if (entries.empty()) {
        entries.push_back(text("  No skills found") | color(theme.color.muted) | dim);
    }

    std::vector<Element> body;
    body.push_back(hbox({
        text("  Skills ") | color(theme.color.primary) | bold,
        filler(),
        text(std::to_string(entries_.size()) + " ") | color(theme.color.dim) | dim,
    }));
    body.push_back(separator());
    body.push_back(vbox(std::move(entries)));
    body.push_back(separator());
    body.push_back(text("  Enter toggle  ·  ↑/↓ select  ·  Esc close")
                       | color(theme.color.dim) | dim);

    return vbox(body) | bgcolor(theme.color.surface)
        | borderRounded | color(theme.color.border);
}

bool SkillsDialog::on_event(ftxui::Event event) {
    if (!showing_) return false;
    if (event == ftxui::Event::Escape) {
        hide();
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        if (selected_ > 0) selected_--;
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        if (selected_ + 1 < static_cast<int>(entries_.size())) selected_++;
        return true;
    }
    if (event == ftxui::Event::Return && !entries_.empty()) {
        auto& info = entries_[selected_];
        if (info.enabled) {
            skills_->disable(info.alias.empty() ? info.name : info.alias);
        } else {
            skills_->enable(info.alias.empty() ? info.name : info.alias);
        }
        refresh();
        if (on_changed_) on_changed_();
        if (selected_ >= static_cast<int>(entries_.size())) {
            selected_ = static_cast<int>(entries_.size()) - 1;
        }
        return true;
    }
    return false;
}

}  // namespace ea::tui
