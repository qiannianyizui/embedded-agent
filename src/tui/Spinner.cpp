// Spinner — animated activity indicator implementation
#include "Spinner.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/animation.hpp>
#include <algorithm>

namespace ea::tui {

const std::vector<std::string> SPINNER_VERBS = {
    "thinking",     "reasoning",   "analyzing",  "planning",
    "researching",  "searching",   "reading",    "writing",
    "reviewing",    "processing",  "computing",  "synthesizing",
    "refactoring",  "testing",     "building",
};

static const std::vector<std::string> SPINNER_BRAILLE = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏",
};

static const std::vector<std::string> SPINNER_DOTS = {
    "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█", "▇", "▆", "▅", "▄", "▃", "▂",
};

const std::vector<std::string> SPINNER_FACES = {
    "(｡•́︿•̀｡)", "(◔_◔)", "(¬‿¬)", "( •_•)>⌐■-■", "(⌐■_■)",
    "(´･_･`)", "◉_◉", "(°ロ°)", "( ˘⌣˘)♡", "ヽ(>∀<☆)☆",
    "٩(๑❛ᴗ❛๑)۶", "(⊙_⊙)", "(¬_¬)", "( ͡° ͜ʖ ͡°)", "ಠ_ಠ",
};

static const std::vector<std::string> SPINNER_EMOJI = {
    "◈", "✦", "⟳", "✎", "⚡",
};

static const std::vector<std::string> SPINNER_ASCII = {
    "|", "/", "-", "\\",
};

std::string tool_verb(const std::string& tool_name) {
    static const struct { const char* name; const char* verb; } map[] = {
        {"browser", "browsing"},
        {"clarify", "asking"},
        {"create_file", "creating"},
        {"delegate_task", "delegating"},
        {"delete_file", "deleting"},
        {"execute_code", "executing"},
        {"image_generate", "generating"},
        {"list_files", "listing"},
        {"memory", "recalling"},
        {"patch", "patching"},
        {"read_file", "reading"},
        {"run_command", "running"},
        {"search_code", "searching"},
        {"search_files", "searching"},
        {"terminal", "running"},
        {"web_extract", "extracting"},
        {"web_search", "searching"},
        {"write_file", "writing"},
        {"shell", "running"},
        {"file", "accessing"},
    };
    for (const auto& m : map) {
        if (tool_name == m.name) return m.verb;
    }
    if (!tool_name.empty()) return tool_name + "…";
    return "working…";
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

static size_t verb_pad_length() {
    size_t max_len = 0;
    for (const auto& v : SPINNER_VERBS) {
        max_len = std::max(max_len, v.size());
    }
    return max_len + 1;  // +1 for trailing "…"
}

static int style_interval_ms(SpinnerStyle style) {
    switch (style) {
        case SpinnerStyle::Unicode: return 90;
        case SpinnerStyle::Dots:    return 70;
        case SpinnerStyle::Kaomoji: return 2500;
        case SpinnerStyle::Emoji:   return 500;
        case SpinnerStyle::Ascii:   return 100;
    }
    return 90;
}

static const std::vector<std::string>& style_frames(SpinnerStyle style) {
    switch (style) {
        case SpinnerStyle::Unicode: return SPINNER_BRAILLE;
        case SpinnerStyle::Dots:    return SPINNER_DOTS;
        case SpinnerStyle::Kaomoji: return SPINNER_FACES;
        case SpinnerStyle::Emoji:   return SPINNER_EMOJI;
        case SpinnerStyle::Ascii:   return SPINNER_ASCII;
    }
    return SPINNER_BRAILLE;
}

// ---------------------------------------------------------------------------
// Frame text computation
// ---------------------------------------------------------------------------

std::string spinner_frame_text(const SpinnerState& state) {
    if (!state.active) return "";

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.started_at).count();
    if (elapsed_ms < 0) elapsed_ms = 0;

    int interval = style_interval_ms(state.style);
    const auto& frames = style_frames(state.style);
    if (frames.empty()) return "";

    std::string result = frames[(elapsed_ms / interval) % frames.size()];

    // Activity label beats the generic verb cycling.
    std::string label = state.activity;
    if (label.empty()) {
        size_t verb_idx = (elapsed_ms / 2500) % SPINNER_VERBS.size();
        label = SPINNER_VERBS[verb_idx] + "…";
    }

    // Pad only the cycling verb (not explicit activities) so the strip
    // doesn't jitter while verbs rotate.
    if (state.activity.empty() && label.size() < verb_pad_length()) {
        label.append(verb_pad_length() - label.size(), ' ');
    }
    result += " " + label;

    if (elapsed_ms >= 1000) {
        result += " · " + fmtDuration(elapsed_ms);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Spinner component — uses FTXUI animation system
// ---------------------------------------------------------------------------

namespace {

struct SpinnerImpl : ftxui::ComponentBase {
    SpinnerState& state;

    explicit SpinnerImpl(SpinnerState& s) : state(s) {}

    ftxui::Element OnRender() override {
        if (!state.active) {
            return ftxui::text("");
        }
        auto& theme = default_theme();
        return ftxui::text(spinner_frame_text(state)) | ftxui::color(theme.color.accent);
    }

    void OnAnimation(ftxui::animation::Params& /*params*/) override {
        if (state.active) {
            ftxui::animation::RequestAnimationFrame();
        }
    }
};

}  // anonymous namespace

ftxui::Component make_spinner(SpinnerState& state) {
    auto comp = ftxui::Make<SpinnerImpl>(state);
    if (state.active) {
        ftxui::animation::RequestAnimationFrame();
    }
    return comp;
}

}  // namespace ea::tui
