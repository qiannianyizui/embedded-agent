// StatusBar — bottom status bar showing token usage, cost, model, session, and busy indicator
#include "StatusBar.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <cmath>
#include <cstdio>
#include <string>

namespace ea::tui {

StatusBar::StatusBar() = default;

ftxui::Component StatusBar::component() {
    if (!component_) {
        component_ = ftxui::Renderer([this] { return render(); });
    }
    return component_;
}

void StatusBar::set_busy(bool busy) {
    busy_ = busy;
}

void StatusBar::update_usage(int input_tokens, int output_tokens) {
    input_tokens_ = input_tokens;
    output_tokens_ = output_tokens;
}

void StatusBar::update_cost(double cost_usd) {
    cost_usd_ = cost_usd;
}

void StatusBar::set_model(const std::string& model) {
    model_ = model;
}

void StatusBar::set_session_id(const std::string& id) {
    session_id_ = id;
}

std::string StatusBar::format_tokens(int tokens) {
    if (tokens >= 1000) {
        double k = static_cast<double>(tokens) / 1000.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fk", k);
        return buf;
    }
    return std::to_string(tokens);
}

std::string StatusBar::format_cost(double cost) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%.4f", cost);
    return buf;
}

ftxui::Element StatusBar::render() {
    using namespace ftxui;

    // Advance tick for spinner animation
    tick_++;

    // Left: busy indicator
    Element indicator;
    if (busy_) {
        indicator = hbox({
            spinner(3, tick_) | color(Color::Yellow),
            text(" working"),
        });
    } else {
        indicator = text("● ready") | color(Color::Green);
    }

    // Middle segments
    std::string fmt_tokens = format_tokens(input_tokens_) + " in / "
                           + format_tokens(output_tokens_) + " out";
    std::string fmt_cost = format_cost(cost_usd_);

    std::vector<Element> segments;
    segments.push_back(std::move(indicator));
    segments.push_back(separator());
    if (!model_.empty()) {
        segments.push_back(text(model_) | dim);
        segments.push_back(separator());
    }
    segments.push_back(text(fmt_tokens) | dim);
    segments.push_back(separator());
    segments.push_back(text(fmt_cost) | dim);

    // Right: session id (right-aligned via filler before it)
    if (!session_id_.empty()) {
        segments.push_back(filler());
        segments.push_back(text(session_id_) | dim);
    }

    return hbox(std::move(segments)) | border | flex;
}

}  // namespace ea::tui
