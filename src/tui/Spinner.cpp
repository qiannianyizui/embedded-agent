// Spinner — animated indicator implementation
#include "Spinner.h"
#include "Theme.h"
#include "FormatUtils.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/animation.hpp>
#include <algorithm>
#include <cstring>

namespace ea::tui {

// ---------------------------------------------------------------------------
// Data — verbs, faces, emoji, braille
// ---------------------------------------------------------------------------

const std::vector<std::string> SPINNER_VERBS = {
    "pondering",      // 9
    "contemplating",  // 12
    "musing",         // 6
    "cogitating",     // 10
    "ruminating",     // 9
    "deliberating",   // 12
    "mulling",        // 7
    "reflecting",     // 10
    "processing",     // 10
    "reasoning",      // 9
    "analyzing",      // 9
    "computing",      // 9
    "synthesizing",   // 12
    "formulating",    // 11
    "brainstorming",  // 13  ← longest
};

const std::vector<std::string> SPINNER_FACES = {
    "(｡•́︿•̀｡)",
    "(◔_◔)",
    "(¬‿¬)",
    "( •_•)>⌐■-■",
    "(⌐■_■)",
    "(´･_･`)",
    "◉_◉",
    "(°ロ°)",
    "( ˘⌣˘)♡",
    "ヽ(>∀<☆)☆",
    "٩(๑❛ᴗ❛๑)۶",
    "(⊙_⊙)",
    "(¬_¬)",
    "( ͡° ͜ʖ ͡°)",
    "ಠ_ಠ",
};

static const std::vector<std::string> SPINNER_EMOJI = {
    "⚕ ", "🌀", "🤔", "✨", "🍵", "🔮",
};

static const std::vector<std::string> SPINNER_ASCII = {
    "|", "/", "-", "\\",
};

static const std::vector<std::string> SPINNER_BRAILLE = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏",
};

// Tool verb mapping
std::string tool_verb(const std::string& tool_name) {
    // Simple mapping; fall back to the tool name with "ing" suffix
    static const struct { const char* name; const char* verb; } map[] = {
        {"browser", "browsing"},
        {"clarify", "asking"},
        {"create_file", "creating"},
        {"delegate_task", "delegating"},
        {"delete_file", "deleting"},
        {"execute_code", "executing"},
        {"image_generate", "generating"},
        {"list_files", "listing"},
        {"memory", "remembering"},
        {"patch", "patching"},
        {"read_file", "reading"},
        {"run_command", "running"},
        {"search_code", "searching"},
        {"search_files", "searching"},
        {"terminal", "terminal"},
        {"web_extract", "extracting"},
        {"web_search", "searching"},
        {"write_file", "writing"},
        {"shell", "running"},
        {"file", "accessing"},
    };
    for (const auto& m : map) {
        if (tool_name == m.name) return m.verb;
    }
    // Fallback: append "ing" if it looks like a verb stem
    if (!tool_name.empty()) {
        return tool_name + "…";
    }
    return "working…";
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

// Verb pad length = max verb length + 1 (for ellipsis)
static size_t verb_pad_length() {
    size_t max_len = 0;
    for (const auto& v : SPINNER_VERBS) {
        max_len = std::max(max_len, v.size());
    }
    return max_len + 1;  // +1 for trailing "…"
}

// Get interval for a given style (in milliseconds)
static int style_interval_ms(SpinnerStyle style) {
    switch (style) {
        case SpinnerStyle::Kaomoji: return 2500;
        case SpinnerStyle::Unicode: return 100;
        case SpinnerStyle::Emoji:   return 600;
        case SpinnerStyle::Ascii:   return 100;
    }
    return 2500;
}

// Does this style show verbs?
static bool style_show_verb(SpinnerStyle style) {
    switch (style) {
        case SpinnerStyle::Kaomoji: return true;
        case SpinnerStyle::Unicode: return false;
        case SpinnerStyle::Emoji:   return true;
        case SpinnerStyle::Ascii:   return true;
    }
    return true;
}

// Get the frame array for a given style
static const std::vector<std::string>& style_frames(SpinnerStyle style) {
    switch (style) {
        case SpinnerStyle::Kaomoji: return SPINNER_FACES;
        case SpinnerStyle::Unicode: return SPINNER_BRAILLE;
        case SpinnerStyle::Emoji:   return SPINNER_EMOJI;
        case SpinnerStyle::Ascii:   return SPINNER_ASCII;
    }
    return SPINNER_FACES;
}

// ---------------------------------------------------------------------------
// Frame text computation
// ---------------------------------------------------------------------------

std::string spinner_verb_text(const SpinnerState& state) {
    if (!style_show_verb(state.style)) return "";

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.started_at).count();

    size_t verb_idx = (elapsed_ms / 2500) % SPINNER_VERBS.size();
    std::string verb = SPINNER_VERBS[verb_idx] + "…";

    // Pad to fixed width
    size_t pad = verb_pad_length();
    if (verb.size() < pad) verb.append(pad - verb.size(), ' ');

    return verb;
}

std::string spinner_frame_text(const SpinnerState& state) {
    if (!state.active) return "";

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.started_at).count();
    if (elapsed_ms < 0) elapsed_ms = 0;

    int interval = style_interval_ms(state.style);
    const auto& frames = style_frames(state.style);
    if (frames.empty()) return "";

    size_t frame_idx = (elapsed_ms / interval) % frames.size();
    std::string result = frames[frame_idx];

    // Append verb if applicable
    std::string verb = spinner_verb_text(state);
    if (!verb.empty()) {
        result += " " + verb;
    }

    // Append duration if started
    if (elapsed_ms > 0) {
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
        std::string frame = spinner_frame_text(state);

        return ftxui::text(frame) | ftxui::color(theme.color.accent);
    }

    void OnAnimation(ftxui::animation::Params& /*params*/) override {
        if (state.active) {
            // Keep requesting frames while active
            ftxui::animation::RequestAnimationFrame();
        }
    }
};

}  // anonymous namespace

ftxui::Component make_spinner(SpinnerState& state) {
    auto comp = ftxui::Make<SpinnerImpl>(state);
    // Kick off the animation loop when first created
    if (state.active) {
        ftxui::animation::RequestAnimationFrame();
    }
    return comp;
}

}  // namespace ea::tui
