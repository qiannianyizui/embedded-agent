// StatusBar — bottom status bar with Hermes-style horizontal rule format
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

void StatusBar::set_busy(bool busy) {
    busy_ = busy;
    if (busy) {
        spinner_state_.start();
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
    if (context_pct_ >= 95) return theme.color.status_critical;
    if (context_pct_ > 80) return theme.color.status_bad;
    if (context_pct_ >= 50) return theme.color.status_warn;
    return theme.color.status_good;
}

ftxui::Element StatusBar::render() {
    using namespace ftxui;
    auto& theme = default_theme();

    // ── Leader ──
    std::vector<Element> segments;
    segments.push_back(text("─ ") | color(theme.color.border));

    // ── Indicator ──
    if (busy_) {
        // Show spinner frame text
        std::string frame = spinner_frame_text(spinner_state_);
        segments.push_back(text(frame) | color(theme.color.accent));
    } else {
        // Idle: show session duration in status_good color
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - session_start_).count();
        segments.push_back(
            text("✓ " + fmtDuration(elapsed_ms)) | color(theme.color.status_good)
        );
    }

    // ── Separator helper ──
    auto sep = [&]() -> Element {
        return text(" │ ") | color(theme.color.muted);
    };

    // ── Model ──
    if (!model_.empty()) {
        segments.push_back(sep());
        segments.push_back(text(shortModelLabel(model_)) | color(theme.color.status_fg));
    }

    // ── Tokens ──
    if (input_tokens_ > 0 || output_tokens_ > 0) {
        segments.push_back(sep());
        std::string tok_str = fmtK(input_tokens_) + "/" + fmtK(output_tokens_);
        segments.push_back(text(tok_str) | color(theme.color.status_fg));
    }

    // ── Context bar ──
    if (context_pct_ >= 0) {
        segments.push_back(sep());
        std::string bar = "[" + ctxBar(context_pct_) + "] "
                        + std::to_string(context_pct_) + "%";
        segments.push_back(text(bar) | color(context_color()));
    }

    // ── Cost ──
    if (cost_usd_ > 0.0) {
        segments.push_back(sep());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "$%.4f", cost_usd_);
        segments.push_back(text(buf) | color(theme.color.status_fg));
    }

    // ── Duration ──
    segments.push_back(sep());
    auto now = std::chrono::steady_clock::now();
    auto session_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - session_start_).count();
    segments.push_back(text(fmtDuration(session_ms)) | color(theme.color.status_fg));

    // ── Right side: cwd ──
    if (!cwd_.empty()) {
        segments.push_back(filler());
        // ─ separator before cwd
        segments.push_back(text(" ─ ") | color(theme.color.border));
        segments.push_back(text(cwd_) | color(theme.color.label));
    }

    return hbox(std::move(segments)) | flex;
}

}  // namespace ea::tui
