// Theme — Hermes-inspired color scheme and brand identity for the TUI
#pragma once
#include <ftxui/dom/deprecated.hpp>  // Color
#include <string>

namespace ea::tui {

// ---------------------------------------------------------------------------
// Color palette — matches Hermes DARK_THEME hex values
// ---------------------------------------------------------------------------
struct ThemeColors {
    ftxui::Color primary         = ftxui::Color::RGB(255, 215, 0);    // #FFD700 Gold
    ftxui::Color accent          = ftxui::Color::RGB(255, 191, 0);    // #FFBF00 Amber
    ftxui::Color border          = ftxui::Color::RGB(205, 127, 50);   // #CD7F32 Copper
    ftxui::Color text            = ftxui::Color::RGB(255, 248, 220);  // #FFF8DC Cornsilk
    ftxui::Color muted           = ftxui::Color::RGB(204, 155, 31);   // #CC9B1F Dark Gold
    ftxui::Color label           = ftxui::Color::RGB(218, 165, 32);   // #DAA520 Goldenrod
    ftxui::Color ok              = ftxui::Color::RGB(76, 175, 80);    // #4caf50
    ftxui::Color error           = ftxui::Color::RGB(239, 83, 80);    // #ef5350
    ftxui::Color warn            = ftxui::Color::RGB(255, 167, 38);   // #ffa726
    ftxui::Color prompt          = ftxui::Color::RGB(255, 248, 220);  // #FFF8DC (same as text)
    ftxui::Color shell_dollar    = ftxui::Color::RGB(77, 171, 247);   // #4dabf7

    // Status bar specific
    ftxui::Color status_good     = ftxui::Color::RGB(143, 188, 143);  // #8FBC8F
    ftxui::Color status_warn     = ftxui::Color::RGB(255, 215, 0);    // #FFD700
    ftxui::Color status_bad      = ftxui::Color::RGB(255, 140, 0);    // #FF8C00
    ftxui::Color status_critical = ftxui::Color::RGB(255, 107, 107);  // #FF6B6B
    ftxui::Color status_fg       = ftxui::Color::RGB(192, 192, 192);  // #C0C0C0
};

// ---------------------------------------------------------------------------
// Brand identity — customizable product name, glyphs, messages
// ---------------------------------------------------------------------------
struct ThemeBrand {
    std::string name       = "Embedded Agent";
    std::string icon       = "⚕";
    std::string prompt     = "❯";         // Composer prompt glyph
    std::string welcome    = "Type your message or /help for commands.";
    std::string goodbye    = "Goodbye! ⚕";
    std::string tool_prefix = "┊";        // Assistant/tool tree glyph
};

// ---------------------------------------------------------------------------
// Role visual mapping — glyph + prefix color + body color per role
// ---------------------------------------------------------------------------
struct RoleStyle {
    char32_t glyph = ' ';          // Leading glyph character
    ftxui::Color prefix_color;     // Glyph color
    ftxui::Color body_color;       // Text color
    bool bold = false;             // Bold glyph?
};

// ---------------------------------------------------------------------------
// Complete theme
// ---------------------------------------------------------------------------
struct Theme {
    ThemeColors color;
    ThemeBrand  brand;

    // Role styles — matches Hermes ROLE mapping
    RoleStyle role_user() const {
        return {U'❯', color.label, color.label, true};
    }
    RoleStyle role_assistant() const {
        return {U'┊', color.border, color.text, false};
    }
    RoleStyle role_system() const {
        return {U'·', color.muted, color.muted, false};
    }
    RoleStyle role_tool() const {
        return {U'⚡', color.muted, color.muted, false};
    }
};

// Returns the singleton default theme (Hermes dark)
const Theme& default_theme();

}  // namespace ea::tui
