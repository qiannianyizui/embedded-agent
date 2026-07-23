// Spinner — animated indicator with 4 styles (kaomoji/unicode/emoji/ascii)
// Matches Hermes FaceTicker behavior: glyph rotation + verb cycling + duration
#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>       // Element
#include <chrono>
#include <string>
#include <vector>

namespace ea::tui {

// Spinner style — determines glyph source and timing
enum class SpinnerStyle {
    Kaomoji,   // 15 kaomoji faces, 2500ms interval + verb cycling (default)
    Unicode,   // Braille dots ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏, ~100ms, no verb
    Emoji,     // ⚕ 🌀 🤔 ✨ 🍵 🔮, 600ms + verb cycling
    Ascii,     // | / - \, 100ms + verb cycling
};

// Spinner state — shared between the component and its consumers
struct SpinnerState {
    bool active = false;            // Is the spinner running?
    SpinnerStyle style = SpinnerStyle::Kaomoji;
    std::chrono::steady_clock::time_point started_at{};  // When activity started

    // Set active + record start time
    void start() {
        active = true;
        started_at = std::chrono::steady_clock::now();
    }
    void stop() {
        active = false;
    }
};

// Create a spinner FTXUI component.
// The component renders the current frame (glyph + verb + duration) when active,
// and an empty element when inactive. It uses RequestAnimationFrame() for
// continuous animation while active.
//
// Usage:
//   SpinnerState state;
//   auto spinner = make_spinner(state);
//   state.start();  // begin animation
//   state.stop();   // end animation
ftxui::Component make_spinner(SpinnerState& state);

// Get the current spinner frame text (for embedding in other renderers)
std::string spinner_frame_text(const SpinnerState& state);

// Get the current verb text (padded)
std::string spinner_verb_text(const SpinnerState& state);

// Verb list — 15 verbs matching Hermes
extern const std::vector<std::string> SPINNER_VERBS;

// Kaomoji list — 15 faces matching Hermes
extern const std::vector<std::string> SPINNER_FACES;

// Tool verb mapping — tool name → verb
std::string tool_verb(const std::string& tool_name);

}  // namespace ea::tui
