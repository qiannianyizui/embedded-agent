// Banner — startup welcome card implementation
#include "Banner.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace ea::tui {

namespace {

ftxui::Element render_info_row(const std::string& key,
                               const std::string& value,
                               ftxui::Color key_color,
                               ftxui::Color value_color) {
    using namespace ftxui;
    return hbox({
        text("  " + key) | color(key_color) | dim,
        text("  ") ,
        text(value) | color(value_color),
    });
}

}  // anonymous namespace

ftxui::Element render_banner(const BannerInfo& info, int width) {
    using namespace ftxui;
    auto& theme = default_theme();
    (void)width;

    std::vector<Element> rows;

    // Brand line
    rows.push_back(hbox({
        text("  " + theme.brand.icon + " ") | color(theme.color.primary) | bold,
        text(theme.brand.name) | color(theme.color.text) | bold,
        filler(),
        text(theme.brand.welcome) | color(theme.color.muted),
        text("  "),
    }));

    rows.push_back(separator());

    if (!info.model.empty()) {
        rows.push_back(render_info_row("model",
            info.model, theme.color.muted, theme.color.accent));
    }
    if (!info.cwd.empty()) {
        rows.push_back(render_info_row("workspace",
            info.cwd, theme.color.muted, theme.color.text));
    }
    if (!info.session_id.empty()) {
        rows.push_back(render_info_row("session",
            shortId(info.session_id), theme.color.muted, theme.color.muted));
    } else {
        rows.push_back(render_info_row("session",
            "(new)", theme.color.muted, theme.color.muted));
    }
    if (info.tool_count > 0) {
        rows.push_back(render_info_row("tools",
            std::to_string(info.tool_count) + " available",
            theme.color.muted, theme.color.muted));
    }

    rows.push_back(separator());

    rows.push_back(hbox({
        text("  shortcuts  ") | color(theme.color.muted) | dim,
        text("⌃P") | color(theme.color.primary) | bold,
        text(" commands  ") | color(theme.color.muted) | dim,
        text("⌃S") | color(theme.color.primary) | bold,
        text(" sessions  ") | color(theme.color.muted) | dim,
        text("⌃C") | color(theme.color.primary) | bold,
        text(" interrupt / exit  ") | color(theme.color.muted) | dim,
        filler(),
        text("  "),
    }));

    return vbox(std::move(rows)) | borderRounded | color(theme.color.border)
        | bgcolor(theme.color.surface);
}

ftxui::Component make_banner(const BannerInfo& info) {
    return ftxui::Renderer([info] {
        return render_banner(info, 90);
    });
}

}  // namespace ea::tui
