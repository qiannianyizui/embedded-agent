// Spinner — animated activity indicator with clean frame styles
#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>       // Element
#include <chrono>
#include <string>
#include <vector>

namespace ea::tui {

// Spinner style — determines glyph source and timing
enum class SpinnerStyle {
    Unicode,   // Braille dots ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ + verb cycling (default)
    Dots,      // ▁▂▃▄▅▆▇█▇▆▅▄▃▂ smooth meter
    Kaomoji,   // 15 kaomoji faces, slow rotation + verb cycling
    Emoji,     // ◈ ✦ ⟳ ✎ ⚡, 600ms + verb cycling
    Ascii,     // | / - \, 100ms + verb cycling
};

// Spinner state — shared between the component and its consumers
struct SpinnerState {
    bool active = false;            // Is the spinner running?
    SpinnerStyle style = SpinnerStyle::Unicode;
    std::string activity;           // Optional label: "analyzing", tool name, ...
    std::chrono::steady_clock::time_point started_at{};  // When activity started

    void start(const std::string& what = "") {
        active = true;
        activity = what;
        started_at = std::chrono::steady_clock::now();
    }
    void stop() {
        active = false;
    }
};

// Create a spinner FTXUI component. Renders the current frame when active,
// an empty element otherwise, and drives RequestAnimationFrame() while active.
ftxui::Component make_spinner(SpinnerState& state);

// Get the current spinner frame text (for embedding in other renderers)
std::string spinner_frame_text(const SpinnerState& state);

// Verb list — activity verbs matching common agent phases
extern const std::vector<std::string> SPINNER_VERBS;

// Kaomoji face list (15 entries — legacy style, available for fun)
extern const std::vector<std::string> SPINNER_FACES;

// Tool verb mapping — tool name → verb
std::string tool_verb(const std::string& tool_name);

}  // namespace ea::tui
