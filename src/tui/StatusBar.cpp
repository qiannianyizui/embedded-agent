// StatusBar — compact activity/usage strip implementation
#include "StatusBar.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <cmath>
#include <cstdio>
#include <string>

namespace ea::tui {

StatusBar::StatusBar() {
    session_start_ = std::chrono::steady_clock::now();
}

ftxui::Component StatusBar::component() {
    if (!component_) {
        component_ = ftxui::Renderer([this] { return render(); });
    }
    return component_;
}

void StatusBar::set_busy(bool busy, const std::string& activity) {
    busy_ = busy;
    if (busy) {
        spinner_state_.start(activity);
    } else {
        spinner_state_.stop();
    }
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

void StatusBar::set_cwd(const std::string& cwd) {
    cwd_ = cwd;
}

void StatusBar::set_context_pct(int pct) {
    context_pct_ = pct;
}

ftxui::Color StatusBar::context_color() const {
    auto& theme = default_theme();
    if (context_pct_ < 0) return theme.color.muted;
    if (context_pct_ >= 95) return theme.color.error;
    if (context_pct_ > 80) return theme.color.warn;
    if (context_pct_ >= 50) return theme.color.accent;
    return theme.color.ok;
}

ftxui::Element StatusBar::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    std::vector<Element> segments;

    auto sep = [&]() -> Element {
        return text("  │  ") | color(theme.color.border);
    };

    // Left: status dot + state
    if (busy_) {
        segments.push_back(text(spinner_frame_text(spinner_state_))
                               | color(theme.color.accent));
    } else {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - session_start_).count();
        segments.push_back(text("● ready") | color(theme.color.ok) | bold);
        segments.push_back(text("  " + fmtDuration(elapsed_ms))
                               | color(theme.color.muted) | dim);
    }

    // Tokens
    if (input_tokens_ > 0 || output_tokens_ > 0) {
        segments.push_back(sep());
        segments.push_back(text("in " + fmtK(input_tokens_)) | color(theme.color.text));
        segments.push_back(text("/out " + fmtK(output_tokens_))
                               | color(theme.color.muted) | dim);
    }

    // Context gauge
    if (context_pct_ >= 0) {
        segments.push_back(sep());
        std::string bar = "ctx[" + ctxBar(context_pct_, 6) + "] "
                        + std::to_string(context_pct_) + "%";
        segments.push_back(text(bar) | color(context_color()));
    }

    // Cost
    if (cost_usd_ > 0.0) {
        segments.push_back(sep());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "$%.4f", cost_usd_);
        segments.push_back(text(buf) | color(theme.color.warn));
    }

    segments.push_back(filler());

    // Right: session id
    if (!session_id_.empty()) {
        segments.push_back(text("#" + shortId(session_id_)) | color(theme.color.muted) | dim);
    }

    return hbox(std::move(segments)) | bgcolor(theme.color.surface);
}

}  // namespace ea::tui
