// SetupWizard — FTXUI multi-step interactive setup wizard
#include "SetupWizard.h"
#include "Theme.h"
#include "Banner.h"
#include "common/io/FileSystem.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <cstdlib>
#include <unistd.h>

namespace ea::tui {

namespace {

// Provider preset info
struct ProviderPreset {
    std::string type;
    std::string label;
    std::string default_url;
    std::string default_model;
    bool needs_api_key;
};

const ProviderPreset PROVIDERS[] = {
    {"openai_compatible", "OpenAI Compatible",  "https://api.openai.com/v1",      "gpt-4o",        true},
    {"anthropic",         "Anthropic Claude",   "https://api.anthropic.com",       "claude-sonnet-5", true},
    {"ollama",            "Ollama (Local)",      "http://localhost:11434",          "llama3",         false},
};

int provider_index(const std::string& type) {
    for (int i = 0; i < 3; ++i) {
        if (PROVIDERS[i].type == type) return i;
    }
    return 0;
}

// Step titles
const char* STEP_TITLES[] = {
    "Welcome",
    "Provider",
    "API Key",
    "Model",
    "Workspace",
    "Security",
    "Review",
};

// Step indicator bar
ftxui::Element render_step_indicator(WizardStep current) {
    using namespace ftxui;
    auto& theme = default_theme();
    std::vector<Element> dots;
    for (int i = 0; i < static_cast<int>(WizardStep::Count); ++i) {
        if (i == static_cast<int>(current)) {
            dots.push_back(text("● ") | color(theme.color.accent) | bold);
        } else if (i < static_cast<int>(current)) {
            dots.push_back(text("● ") | color(theme.color.ok));
        } else {
            dots.push_back(text("● ") | color(theme.color.muted) | dim);
        }
    }
    return hbox({
        text("─ ") | color(theme.color.border),
        hbox(std::move(dots)),
        text(STEP_TITLES[static_cast<int>(current)]) | color(theme.color.label) | bold,
        filler(),
    });
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Non-interactive setup
// ---------------------------------------------------------------------------

ea::config::AppConfig run_non_interactive_setup() {
    ea::config::AppConfig cfg;

    const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
    if (api_key && api_key[0] != '\0') cfg.provider.api_key = api_key;
    const char* model = getenv("EMBEDDED_AGENT_MODEL");
    if (model && model[0] != '\0') cfg.agent.model = model;
    const char* base_url = getenv("EMBEDDED_AGENT_BASE_URL");
    if (base_url && base_url[0] != '\0') cfg.provider.base_url = base_url;
    const char* provider_type = getenv("EMBEDDED_AGENT_PROVIDER");
    if (provider_type && provider_type[0] != '\0') cfg.provider.type = provider_type;

    auto cfg_dir = ea::fs::config_dir();
    if (cfg_dir.ok()) {
        cfg.config_path = cfg_dir.value() + "/config.toml";
    }

    if (cfg.agent.model.empty() && !cfg.provider.default_model.empty()) {
        cfg.agent.model = cfg.provider.default_model;
    }
    if (cfg.provider.default_model.empty() && !cfg.agent.model.empty()) {
        cfg.provider.default_model = cfg.agent.model;
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Build AppConfig from wizard state
// ---------------------------------------------------------------------------

ea::config::AppConfig build_config_from_wizard(const WizardState& state) {
    ea::config::AppConfig cfg;

    cfg.provider.type = state.provider_type;
    cfg.provider.base_url = state.base_url;
    cfg.provider.api_key = state.api_key;
    cfg.provider.default_model = state.default_model;
    cfg.agent.model = state.default_model;
    cfg.security.autonomy = state.autonomy;
    if (!state.workspace.empty()) {
        cfg.security.workspace = state.workspace;
    }

    auto cfg_dir = ea::fs::config_dir();
    if (cfg_dir.ok()) {
        cfg.config_path = cfg_dir.value() + "/config.toml";
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Wizard component implementation
//
// Focus routing:
//   - The content area holds either a selection list (Provider/Security) or
//     an Input field (ApiKey/Model/Workspace) or static text (Welcome/Review).
//   - The footer holds navigation buttons (Back/Next/Finish + Cancel) as real
//     FTXUI Button components so they receive focus and can be activated with
//     Enter or a mouse click.
//   - A Container::Vertical chains content -> buttons. Tab/Down moves focus
//     from the content area to the button row; Left/Right moves between the
//     buttons in the horizontal button container.
// ---------------------------------------------------------------------------

namespace {

struct WizardImpl : ftxui::ComponentBase {
    WizardState& state;
    ftxui::ScreenInteractive* get_screen() { return state.screen; }
    int selected_provider = 0;
    int selected_autonomy = 0;  // 0=supervised, 1=autonomous, 2=full

    // Input fields
    std::string input_url;
    std::string input_key;
    std::string input_model;
    std::string input_workspace;

    // Input components
    ftxui::Component key_input_;
    ftxui::Component model_input_;
    ftxui::Component workspace_input_;

    // Navigation buttons
    ftxui::Component back_btn_;
    ftxui::Component next_btn_;
    ftxui::Component finish_btn_;
    ftxui::Component cancel_btn_;
    ftxui::Component button_row_;     // horizontal container of buttons
    ftxui::Component content_area_;   // holds the active step's focusable child
    ftxui::Component container_;      // vertical: content_area -> button_row
    int container_selector_ = 0;      // 0=content_area_, 1=button_row_

    explicit WizardImpl(WizardState& s) : state(s) {
        using namespace ftxui;

        selected_provider = provider_index(state.provider_type);
        input_url = state.base_url;
        input_key = state.api_key;
        input_model = state.default_model;

        char cwd_buf[4096];
        if (getcwd(cwd_buf, sizeof(cwd_buf))) {
            input_workspace = state.workspace.empty() ? cwd_buf : state.workspace;
        } else if (!state.workspace.empty()) {
            input_workspace = state.workspace;
        }

        if (state.autonomy == "autonomous") selected_autonomy = 1;
        else if (state.autonomy == "full") selected_autonomy = 2;
        else selected_autonomy = 0;

        // Input components
        InputOption key_opt;
        key_opt.placeholder = "sk-...";
        key_opt.multiline = false;
        key_input_ = Input(&input_key, key_opt);

        InputOption model_opt;
        model_opt.placeholder = "gpt-4o";
        model_opt.multiline = false;
        model_input_ = Input(&input_model, model_opt);

        InputOption ws_opt;
        ws_opt.placeholder = "/home/user/project";
        ws_opt.multiline = false;
        workspace_input_ = Input(&input_workspace, ws_opt);

        // Navigation buttons — custom transform: focused button gets white
        // background with dark text for clear visual feedback.
        auto btn_style = ButtonOption::Ascii();
        btn_style.transform = [](const EntryState& s) {
            auto element = text(" " + s.label + " ");
            if (s.focused) {
                element = element | inverted | bold;
            } else if (s.active) {
                element = element | bold;
            }
            return element;
        };
        back_btn_ = Button(" < Back > ", [this] { go_back(); }, btn_style);
        next_btn_ = Button(" < Next > ", [this] { go_next(); }, btn_style);
        finish_btn_ = Button(" < Finish > ", [this] {
            sync_state();
            state.finished = true;
            if (get_screen()) get_screen()->Exit();
        }, btn_style);
        cancel_btn_ = Button(" < Cancel > ", [this] {
            state.cancelled = true;
            if (get_screen()) get_screen()->Exit();
        }, btn_style);

        // Button row: horizontal container, focus moves Left/Right between buttons
        button_row_ = Container::Horizontal({});
        // Rebuilt per-step in rebuild_layout()

        // Content area: holds the active step's focusable child (Input or a
        // dummy selectable for arrow-key steps). Rebuilt per-step.
        content_area_ = Container::Vertical({});

        // Top-level: content_area on top, button_row on bottom. Down/Tab moves
        // focus from content to buttons. container_selector_ controls which
        // child is active: 0=content_area_, 1=button_row_.
        container_ = Container::Vertical({content_area_, button_row_}, &container_selector_);

        // Wrap container with a renderer + global key handling.
        auto inner = Renderer(container_, [this] { return render_frame(); });
        auto with_events = CatchEvent(inner, [this](Event e) { return on_global_event(e); });
        Add(with_events);

        rebuild_layout();
    }

    // Rebuild content_area_ and button_row_ children for the current step
    void rebuild_layout() {
        using namespace ftxui;

        // ---- Content area ----
        content_area_->DetachAllChildren();
        bool has_input = false;
        switch (state.current_step) {
            case WizardStep::ApiKey:
                content_area_->Add(key_input_);
                has_input = true;
                break;
            case WizardStep::Model:
                content_area_->Add(model_input_);
                has_input = true;
                break;
            case WizardStep::Workspace:
                content_area_->Add(workspace_input_);
                has_input = true;
                break;
            default:
                // Welcome / Provider / Security / Review have no text input.
                // Add a non-focusable placeholder.
                content_area_->Add(Renderer([] { return text(""); }));
                break;
        }

        // ---- Button row ----
        button_row_->DetachAllChildren();
        if (state.current_step != WizardStep::Welcome) {
            button_row_->Add(back_btn_);
        }
        if (state.current_step == WizardStep::Review) {
            button_row_->Add(finish_btn_);
        } else if (state.current_step != WizardStep::Welcome) {
            button_row_->Add(next_btn_);
        } else {
            button_row_->Add(next_btn_);  // "Get Started"
        }
        button_row_->Add(cancel_btn_);

        // ---- Focus routing ----
        // container_ = Vertical({content_area_, button_row_}), selector 0→content, 1→buttons.
        // For steps with text input, focus starts in content_area_ (the Input field).
        // For steps without text input, focus goes directly to button_row_ so the
        // user can immediately press Enter to activate the default button.
        if (has_input) {
            container_selector_ = 0;  // focus on content_area_ (Input field)
        } else {
            container_selector_ = 1;  // focus on button_row_
        }
    }

    // Global event handling — arrow selection for Provider/Security steps,
    // plus Enter-to-advance convenience on those steps.
    bool on_global_event(ftxui::Event event) {
        using namespace ftxui;

        // Escape always cancels and exits
        if (event == Event::Escape) {
            state.cancelled = true;
            if (get_screen()) get_screen()->Exit();
            return true;
        }

        // Provider step: ↑/↓ to pick provider
        if (state.current_step == WizardStep::Provider) {
            if (event == Event::ArrowUp && selected_provider > 0) {
                selected_provider--;
                apply_provider_preset();
                return true;
            }
            if (event == Event::ArrowDown && selected_provider < 2) {
                selected_provider++;
                apply_provider_preset();
                return true;
            }
        }

        // Security step: ↑/↓ to pick autonomy
        if (state.current_step == WizardStep::Security) {
            if (event == Event::ArrowUp && selected_autonomy > 0) {
                selected_autonomy--;
                return true;
            }
            if (event == Event::ArrowDown && selected_autonomy < 2) {
                selected_autonomy++;
                return true;
            }
        }

        // For steps without text input (Welcome/Provider/Security/Review),
        // forward keyboard events directly to the button row so the user
        // doesn't need to press Tab/↓ first to reach the buttons.
        bool has_input = (state.current_step == WizardStep::ApiKey ||
                          state.current_step == WizardStep::Model ||
                          state.current_step == WizardStep::Workspace);
        if (!has_input) {
            if (button_row_->OnEvent(event)) return true;
        }

        return false;
    }

    void go_next() {
        int step = static_cast<int>(state.current_step);
        if (step == static_cast<int>(WizardStep::Provider) && selected_provider == 2) {
            step = static_cast<int>(WizardStep::Model);  // skip ApiKey for Ollama
        } else {
            step++;
        }
        if (step < static_cast<int>(WizardStep::Count)) {
            state.current_step = static_cast<WizardStep>(step);
            rebuild_layout();
        }
    }

    void go_back() {
        int step = static_cast<int>(state.current_step);
        if (step == static_cast<int>(WizardStep::Model) && selected_provider == 2) {
            step = static_cast<int>(WizardStep::Provider);
        } else {
            step--;
        }
        if (step >= 0) {
            state.current_step = static_cast<WizardStep>(step);
            rebuild_layout();
        }
    }

    void apply_provider_preset() {
        const auto& p = PROVIDERS[selected_provider];
        state.provider_type = p.type;
        if (input_url.empty()) input_url = p.default_url;
        if (input_model.empty()) input_model = p.default_model;
    }

    void sync_state() {
        state.provider_type = PROVIDERS[selected_provider].type;
        state.base_url = input_url;
        state.api_key = input_key;
        state.default_model = input_model;
        state.workspace = input_workspace;
        const char* autonomy_levels[] = {"supervised", "autonomous", "full"};
        state.autonomy = autonomy_levels[selected_autonomy];
    }

    // ---- Frame renderer ----
    ftxui::Element render_frame() {
        using namespace ftxui;
        auto& theme = default_theme();
        sync_state();

        Element body;
        switch (state.current_step) {
            case WizardStep::Welcome:   body = render_welcome(); break;
            case WizardStep::Provider:  body = render_provider(); break;
            case WizardStep::ApiKey:    body = render_api_key(); break;
            case WizardStep::Model:     body = render_model(); break;
            case WizardStep::Workspace: body = render_workspace(); break;
            case WizardStep::Security:  body = render_security(); break;
            case WizardStep::Review:    body = render_review(); break;
            default: body = text(""); break;
        }

        auto step_bar = render_step_indicator(state.current_step);

        // Determine which button label shows for "Next"
        return vbox({
            step_bar,
            separator(),
            body | flex,
            separator(),
            // Render the button row via its component (preserves focus state)
            button_row_->Render(),
        }) | borderRounded | color(theme.color.border) | size(WIDTH, GREATER_THAN, 60);
    }

    // ---- Step renderers ----

    ftxui::Element render_welcome() {
        using namespace ftxui;
        auto& theme = default_theme();
        std::vector<Element> lines;
        lines.push_back(text(""));
        lines.push_back(text("  ⚕  Embedded Agent Setup Wizard") | color(theme.color.primary) | bold);
        lines.push_back(text(""));
        lines.push_back(text("  Let's configure your Embedded Agent installation.") | color(theme.color.text));
        lines.push_back(text("  Use Tab or ↓ to move to the buttons, Enter to activate.") | color(theme.color.muted) | dim);
        lines.push_back(text("  Press Esc at any time to cancel.") | color(theme.color.muted) | dim);
        lines.push_back(text(""));

        if (state.has_openclaw) {
            lines.push_back(text("  ◆ OpenClaw Installation Detected") | color(theme.color.warn));
            lines.push_back(text("    Found OpenClaw data at ~/.openclaw") | color(theme.color.muted) | dim);
            lines.push_back(text(""));
        }
        if (state.has_existing_config) {
            lines.push_back(text("  ◆ Existing Configuration Found") | color(theme.color.accent));
            lines.push_back(text("    Current values will be shown as defaults.") | color(theme.color.muted) | dim);
            lines.push_back(text(""));
        }
        return vbox(std::move(lines));
    }

    ftxui::Element render_provider() {
        using namespace ftxui;
        auto& theme = default_theme();
        std::vector<Element> entries;
        for (int i = 0; i < 3; ++i) {
            const auto& p = PROVIDERS[i];
            if (i == selected_provider) {
                entries.push_back(hbox({
                    text("▸ ") | color(theme.color.accent) | bold,
                    text(p.label) | inverted | bold | color(theme.color.accent),
                }));
            } else {
                entries.push_back(hbox({
                    text("  "),
                    text(p.label) | color(theme.color.text),
                }));
            }
        }
        return vbox({
            text("  Select your AI provider") | color(theme.color.primary) | bold,
            text(""),
            vbox(std::move(entries)),
            text(""),
            text("  Use ↑/↓ to select, then Tab to the Next button") | color(theme.color.muted) | dim,
        });
    }

    ftxui::Element render_api_key() {
        using namespace ftxui;
        auto& theme = default_theme();
        const auto& p = PROVIDERS[selected_provider];
        return vbox({
            text("  Enter your API key") | color(theme.color.primary) | bold,
            text("  Provider: " + p.label) | color(theme.color.muted),
            text(""),
            hbox({
                text("  ❯ ") | color(theme.color.label) | bold,
                key_input_->Render() | flex,
            }),
            text(""),
            text("  Your key is stored locally in config.toml") | color(theme.color.muted) | dim,
        });
    }

    ftxui::Element render_model() {
        using namespace ftxui;
        auto& theme = default_theme();
        const auto& p = PROVIDERS[selected_provider];
        return vbox({
            text("  Select default model") | color(theme.color.primary) | bold,
            text("  Provider: " + p.label) | color(theme.color.muted),
            text(""),
            hbox({
                text("  ❯ ") | color(theme.color.label) | bold,
                model_input_->Render() | flex,
            }),
            text(""),
            text("  Recommended: " + p.default_model) | color(theme.color.muted) | dim,
        });
    }

    ftxui::Element render_workspace() {
        using namespace ftxui;
        auto& theme = default_theme();
        return vbox({
            text("  Set workspace directory") | color(theme.color.primary) | bold,
            text("  The agent will operate within this directory") | color(theme.color.muted),
            text(""),
            hbox({
                text("  ❯ ") | color(theme.color.label) | bold,
                workspace_input_->Render() | flex,
            }),
            text(""),
            text("  Leave empty to use current directory") | color(theme.color.muted) | dim,
        });
    }

    ftxui::Element render_security() {
        using namespace ftxui;
        auto& theme = default_theme();
        const char* autonomy_labels[] = {"Supervised", "Autonomous", "Full"};
        const char* autonomy_descs[] = {
            "Ask for approval before dangerous operations",
            "Auto-approve safe operations, ask for dangerous ones",
            "Auto-approve all operations (use with caution)",
        };
        std::vector<Element> entries;
        for (int i = 0; i < 3; ++i) {
            if (i == selected_autonomy) {
                entries.push_back(hbox({
                    text("▸ ") | color(theme.color.accent) | bold,
                    text(autonomy_labels[i]) | inverted | bold | color(theme.color.accent),
                }));
                entries.push_back(text("    " + std::string(autonomy_descs[i])) | color(theme.color.muted) | dim);
            } else {
                entries.push_back(hbox({
                    text("  "),
                    text(autonomy_labels[i]) | color(theme.color.text),
                }));
                entries.push_back(text("    " + std::string(autonomy_descs[i])) | color(theme.color.muted) | dim);
            }
            entries.push_back(text(""));
        }
        return vbox({
            text("  Select security level") | color(theme.color.primary) | bold,
            text("  Use ↑/↓ to select") | color(theme.color.muted) | dim,
            text(""),
            vbox(std::move(entries)),
        });
    }

    ftxui::Element render_review() {
        using namespace ftxui;
        auto& theme = default_theme();
        sync_state();
        auto config = build_config_from_wizard(state);

        std::vector<Element> lines;
        lines.push_back(text("  Review your configuration") | color(theme.color.primary) | bold);
        lines.push_back(text(""));
        lines.push_back(text("  Provider") | color(theme.color.label) | bold);
        lines.push_back(hbox({ text("    type:       ") | color(theme.color.muted),
                               text(config.provider.type) | color(theme.color.text) }));
        if (!config.provider.base_url.empty())
            lines.push_back(hbox({ text("    base_url:   ") | color(theme.color.muted),
                                   text(config.provider.base_url) | color(theme.color.text) }));
        if (!config.provider.api_key.empty())
            lines.push_back(hbox({ text("    api_key:    ") | color(theme.color.muted),
                                   text(config.provider.api_key.substr(0, 8) + "…") | color(theme.color.text) }));
        lines.push_back(hbox({ text("    model:      ") | color(theme.color.muted),
                               text(config.agent.model) | color(theme.color.text) }));
        lines.push_back(text(""));
        lines.push_back(text("  Security") | color(theme.color.label) | bold);
        lines.push_back(hbox({ text("    autonomy:   ") | color(theme.color.muted),
                               text(config.security.autonomy) | color(theme.color.text) }));
        if (!config.security.workspace.empty())
            lines.push_back(hbox({ text("    workspace:  ") | color(theme.color.muted),
                                   text(config.security.workspace) | color(theme.color.text) }));
        lines.push_back(text(""));
        lines.push_back(text("  Tab to Finish and press Enter to save") | color(theme.color.accent));
        return vbox(std::move(lines));
    }
};

}  // anonymous namespace

ftxui::Component make_setup_wizard(WizardState& state) {
    return ftxui::Make<WizardImpl>(state);
}

}  // namespace ea::tui
