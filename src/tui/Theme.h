// Theme — modern dark-IDE palette and brand identity for the TUI
// Design language: calm midnight surfaces, indigo primary + cyan accent,
// semantic colors for status, clear typographic hierarchy.
#pragma once
#include <ftxui/dom/deprecated.hpp>  // Color
#include <string>

namespace ea::tui {

// ---------------------------------------------------------------------------
// Color palette — midnight / indigo
// ---------------------------------------------------------------------------
struct ThemeColors {
    // Surfaces
    ftxui::Color bg          = ftxui::Color::RGB(13, 17, 23);    // #0D1117
    ftxui::Color surface     = ftxui::Color::RGB(22, 27, 38);    // #161B26
    ftxui::Color surface_alt = ftxui::Color::RGB(27, 34, 48);    // #1B2230
    ftxui::Color border      = ftxui::Color::RGB(42, 51, 70);    // #2A3346
    ftxui::Color border_soft = ftxui::Color::RGB(32, 40, 57);    // #202839

    // Text
    ftxui::Color text        = ftxui::Color::RGB(232, 234, 242); // #E8EAF2
    ftxui::Color muted       = ftxui::Color::RGB(138, 148, 168); // #8A94A8
    ftxui::Color dim         = ftxui::Color::RGB(91, 100, 116);  // #5B6474
    ftxui::Color label       = ftxui::Color::RGB(166, 173, 232); // #A6ADE8 lavender

    // Semantic accents
    ftxui::Color primary     = ftxui::Color::RGB(139, 140, 248); // #8B8CF8 indigo
    ftxui::Color accent      = ftxui::Color::RGB(76, 201, 240);  // #4CC9F0 cyan
    ftxui::Color ok          = ftxui::Color::RGB(74, 222, 128);  // #4ADE80
    ftxui::Color warn        = ftxui::Color::RGB(251, 191, 36);  // #FBBF24
    ftxui::Color error       = ftxui::Color::RGB(248, 113, 113); // #F87171

    // Message-specific tints
    ftxui::Color user_bg     = ftxui::Color::RGB(40, 44, 68);    // indigo bubble
    ftxui::Color user_fg     = ftxui::Color::RGB(207, 209, 238);
    ftxui::Color tool_bg     = ftxui::Color::RGB(17, 21, 30);
    ftxui::Color rail        = ftxui::Color::RGB(92, 96, 186);   // assistant left rail

    // Prompts / input
    ftxui::Color prompt      = ftxui::Color::RGB(139, 140, 248);
    ftxui::Color shell       = ftxui::Color::RGB(76, 201, 240);
};

// ---------------------------------------------------------------------------
// Brand identity — name, glyphs, copy
// ---------------------------------------------------------------------------
struct ThemeBrand {
    std::string name       = "Embedded Agent";
    std::string icon       = "◈";
    std::string prompt     = "❯";
    std::string welcome    = "What would you like to do?";
    std::string goodbye    = "Session ended — see you next time.";
    std::string user_chip  = "you";
    std::string agent_chip = "agent";
    std::string tool_chip  = "tool";
    std::string sys_chip   = "system";
    std::string error_chip = "error";
};

// ---------------------------------------------------------------------------
// Role visual mapping — chip label + accent rail + body color
// ---------------------------------------------------------------------------
struct RoleStyle {
    std::string label;           // small chip text
    ftxui::Color chip_color;     // chip text color
    ftxui::Color accent_color;   // left rail / glyph
    ftxui::Color body_color;     // message text
};

// ---------------------------------------------------------------------------
// Complete theme
// ---------------------------------------------------------------------------
struct Theme {
    ThemeColors color;
    ThemeBrand  brand;

    RoleStyle role_user() const {
        return {brand.user_chip, color.primary, color.user_fg, color.user_fg};
    }
    RoleStyle role_assistant() const {
        return {brand.agent_chip, color.accent, color.rail, color.text};
    }
    RoleStyle role_tool() const {
        return {brand.tool_chip, color.muted, color.muted, color.muted};
    }
    RoleStyle role_system() const {
        return {brand.sys_chip, color.muted, color.muted, color.muted};
    }
    RoleStyle role_error() const {
        return {brand.error_chip, color.error, color.error, color.error};
    }
};

// Returns the singleton default theme (midnight)
const Theme& default_theme();

}  // namespace ea::tui
