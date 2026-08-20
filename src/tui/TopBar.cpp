// TopBar — one-line app header implementation
#include "TopBar.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace ea::tui {

TopBar::TopBar(SpinnerState& spinner)
    : spinner_(spinner) {}

ftxui::Component TopBar::component() {
    if (!component_) {
        component_ = ftxui::Renderer([this] { return render(); });
    }
    return component_;
}

void TopBar::set_busy(bool busy, const std::string& activity) {
    busy_ = busy;
    if (busy) {
        spinner_.start(activity);
    } else {
        spinner_.stop();
    }
}

void TopBar::set_model(const std::string& model) {
    model_ = model;
}

void TopBar::set_mode(const std::string& mode) {
    mode_ = mode;
}

void TopBar::set_session_id(const std::string& id) {
    session_id_ = id;
}

ftxui::Element TopBar::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    std::vector<Element> parts;

    // Brand
    parts.push_back(text("  " + theme.brand.icon + " ") | color(theme.color.primary) | bold);
    parts.push_back(text(theme.brand.name) | color(theme.color.text) | bold);

    // Status
    parts.push_back(text("  "));
    if (busy_) {
        parts.push_back(text("⟳") | color(theme.color.accent) | bold);
    } else {
        parts.push_back(text("●") | color(theme.color.ok) | bold);
    }
    if (mode_ == "plan") {
        parts.push_back(text("  PLAN") | color(theme.color.accent) | bold);
    } else if (mode_ == "acceptEdits") {
        parts.push_back(text("  ACCEPT EDITS") | color(theme.color.warn) | bold);
    } else if (mode_ == "bypassPermissions") {
        parts.push_back(text("  BYPASS") | color(theme.color.error) | bold);
    }

    // Model / session
    if (!model_.empty()) {
        parts.push_back(text("  ") );
        parts.push_back(text(shortModelLabel(model_)) | color(theme.color.muted));
    }
    if (!session_id_.empty()) {
        parts.push_back(text("  #" + shortId(session_id_)) | color(theme.color.muted) | dim);
    }

    parts.push_back(filler());

    // Key hints (right-aligned, low emphasis)
    parts.push_back(text("S-TAB plan/build") | color(theme.color.dim) | dim);
    parts.push_back(text("  ") | color(theme.color.dim) | dim);
    parts.push_back(text("/mode") | color(theme.color.dim) | dim);
    parts.push_back(text("  ") | color(theme.color.dim) | dim);
    parts.push_back(text("⌃C stop") | color(theme.color.dim) | dim);
    parts.push_back(text("  "));

    return hbox(std::move(parts)) | bgcolor(theme.color.surface);
}

}  // namespace ea::tui
