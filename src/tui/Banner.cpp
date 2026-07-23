// Banner — startup brand display implementation
#include "Banner.h"
#include "Theme.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace ea::tui {

namespace {

// Compact ASCII art for "Embedded Agent" — fits in ~70 cols
// 6 lines, drawn with ┃ ━ ╋ ┏ ┓ ┗ ┛ ┣ ┫ ┳ ┻ ╭ ╮ ╰ ╯
// Simplified version that's readable and themed
std::vector<std::string> ascii_art_lines() {
    return {
        "╺━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╸",
        "                                                                 ",
        "   ╭─────────────────────────────────────────────────────────╮   ",
        "   │  ⚕  E M B E D D E D   A G E N T                       │   ",
        "   ╰─────────────────────────────────────────────────────────╯   ",
        "                                                                 ",
    };
}

ftxui::Element render_wide_banner(const BannerInfo& info) {
    using namespace ftxui;
    auto& theme = default_theme();

    auto lines = ascii_art_lines();
    std::vector<Element> art_elements;
    for (size_t i = 0; i < lines.size(); ++i) {
        // Gradient: line 0=primary, 1=accent, 2-3=border, 4=accent, 5=primary
        Color c;
        switch (i) {
            case 0: case 5: c = theme.color.primary; break;
            case 1: case 4: c = theme.color.accent; break;
            default: c = theme.color.border; break;
        }
        art_elements.push_back(text(lines[i]) | color(c));
    }

    // Info panel below the art
    std::vector<Element> info_elements;
    if (!info.model.empty()) {
        info_elements.push_back(hbox({
            text("  model  ") | color(theme.color.muted) | dim,
            text(info.model) | color(theme.color.text),
        }));
    }
    if (!info.cwd.empty()) {
        info_elements.push_back(hbox({
            text("  cwd    ") | color(theme.color.muted) | dim,
            text(info.cwd) | color(theme.color.label),
        }));
    }
    if (!info.session_id.empty()) {
        info_elements.push_back(hbox({
            text("  session") | color(theme.color.muted) | dim,
            text(" " + info.session_id) | color(theme.color.status_fg),
        }));
    }
    if (info.tool_count > 0) {
        info_elements.push_back(hbox({
            text("  tools  ") | color(theme.color.muted) | dim,
            text(std::to_string(info.tool_count) + " available") | color(theme.color.status_fg),
        }));
    }

    // Welcome message
    info_elements.push_back(text(""));
    info_elements.push_back(text("  " + theme.brand.welcome) | color(theme.color.muted));
    info_elements.push_back(text(""));

    auto art = vbox(std::move(art_elements));
    auto info_panel = vbox(std::move(info_elements));

    return vbox({art, info_panel});
}

ftxui::Element render_medium_banner(const BannerInfo& /*info*/) {
    using namespace ftxui;
    auto& theme = default_theme();

    // ─── Embedded Agent ───
    std::string name = " " + theme.brand.name + " ";
    int dash_count = std::max(3, (60 - static_cast<int>(name.size())) / 2);
    std::string dashes;
    for (int i = 0; i < dash_count; ++i) dashes += "─";

    return hbox({
        text(dashes) | color(theme.color.border),
        text(name) | color(theme.color.primary) | bold,
        text(dashes) | color(theme.color.border),
    });
}

ftxui::Element render_narrow_banner() {
    using namespace ftxui;
    auto& theme = default_theme();
    return text(theme.brand.name) | color(theme.color.primary) | bold;
}

}  // anonymous namespace

ftxui::Element render_banner(const BannerInfo& info, int width) {
    if (width >= 95) {
        return render_wide_banner(info);
    } else if (width >= 40) {
        return render_medium_banner(info);
    } else {
        return render_narrow_banner();
    }
}

ftxui::Component make_banner(const BannerInfo& info) {
    // Capture info by value for the lambda
    return ftxui::Renderer([info] {
        // Terminal width — use a reasonable default
        // The actual width is determined at render time; FTXUI doesn't
        // provide it directly in Renderer. Use 95 as fallback.
        return render_banner(info, 95);
    });
}

}  // namespace ea::tui
