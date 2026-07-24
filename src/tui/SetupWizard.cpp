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

// Provider selection renderer
ftxui::Element render_provider_step(WizardState& /*state*/, int& selected_provider) {
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
        text("  Use ↑/↓ to select, then press Enter") | color(theme.color.muted) | dim,
    });
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Non-interactive setup
// ---------------------------------------------------------------------------

ea::config::AppConfig run_non_interactive_setup() {
    ea::config::AppConfig cfg;

    // Apply environment variables
    const char* api_key = getenv("EMBEDDED_AGENT_API_KEY");
    if (api_key && api_key[0] != '\0') cfg.provider.api_key = api_key;
    const char* model = getenv("EMBEDDED_AGENT_MODEL");
    if (model && model[0] != '\0') cfg.agent.model = model;
    const char* base_url = getenv("EMBEDDED_AGENT_BASE_URL");
    if (base_url && base_url[0] != '\0') cfg.provider.base_url = base_url;
    const char* provider_type = getenv("EMBEDDED_AGENT_PROVIDER");
    if (provider_type && provider_type[0] != '\0') cfg.provider.type = provider_type;

    // Set config path
    auto cfg_dir = ea::fs::config_dir();
    if (cfg_dir.ok()) {
        cfg.config_path = cfg_dir.value() + "/config.toml";
    }

    // Set default model from provider
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

    // Provider
    cfg.provider.type = state.provider_type;
    cfg.provider.base_url = state.base_url;
    cfg.provider.api_key = state.api_key;
    cfg.provider.default_model = state.default_model;

    // Agent
    cfg.agent.model = state.default_model;

    // Security
    cfg.security.autonomy = state.autonomy;
    if (!state.workspace.empty()) {
        cfg.security.workspace = state.workspace;
    }

    // Config path
    auto cfg_dir = ea::fs::config_dir();
    if (cfg_dir.ok()) {
        cfg.config_path = cfg_dir.value() + "/config.toml";
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Wizard component implementation
// ---------------------------------------------------------------------------

namespace {

struct WizardImpl : ftxui::ComponentBase {
    WizardState& state;
    int selected_provider = 0;

    // Input fields per step
    std::string input_url;
    std::string input_key;
    std::string input_model;
    std::string input_workspace;
    int selected_autonomy = 0;  // 0=supervised, 1=autonomous, 2=full

    // FTXUI input components
    ftxui::Component url_input_;
    ftxui::Component key_input_;
    ftxui::Component model_input_;
    ftxui::Component workspace_input_;

    explicit WizardImpl(WizardState& s) : state(s) {
        // Initialize from state
        selected_provider = provider_index(state.provider_type);
        input_url = state.base_url;
        input_key = state.api_key;
        input_model = state.default_model;

        // Get cwd as default workspace
        char cwd_buf[4096];
        if (getcwd(cwd_buf, sizeof(cwd_buf))) {
            input_workspace = state.workspace.empty() ? cwd_buf : state.workspace;
        }

        // Initialize selected_autonomy from state
        if (state.autonomy == "autonomous") selected_autonomy = 1;
        else if (state.autonomy == "full") selected_autonomy = 2;
        else selected_autonomy = 0;

        // Create input components
        ftxui::InputOption url_opt;
        url_opt.placeholder = "https://api.openai.com/v1";
        url_opt.multiline = false;
        url_input_ = ftxui::Input(&input_url, url_opt);

        ftxui::InputOption key_opt;
        key_opt.placeholder = "sk-...";
        key_opt.multiline = false;
        key_input_ = ftxui::Input(&input_key, key_opt);

        ftxui::InputOption model_opt;
        model_opt.placeholder = "gpt-4o";
        model_opt.multiline = false;
        model_input_ = ftxui::Input(&input_model, model_opt);

        ftxui::InputOption ws_opt;
        ws_opt.placeholder = "/home/user/project";
        ws_opt.multiline = false;
        workspace_input_ = ftxui::Input(&input_workspace, ws_opt);
    }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        auto& theme = default_theme();

        // Sync state from inputs
        sync_state();

        Element body;
        switch (state.current_step) {
            case WizardStep::Welcome:  body = render_welcome(); break;
            case WizardStep::Provider: body = render_provider(); break;
            case WizardStep::ApiKey:   body = render_api_key(); break;
            case WizardStep::Model:    body = render_model(); break;
            case WizardStep::Workspace:body = render_workspace(); break;
            case WizardStep::Security: body = render_security(); break;
            case WizardStep::Review:   body = render_review(); break;
            default: body = text(""); break;
        }

        auto step_bar = render_step_indicator(state.current_step);

        // Navigation buttons
        std::vector<Element> nav;
        if (state.current_step != WizardStep::Welcome) {
            nav.push_back(text(" < Back ") | color(theme.color.muted));
            nav.push_back(text("  "));
        }
        if (state.current_step == WizardStep::Review) {
            nav.push_back(text(" Finish ") | color(theme.color.ok) | bold);
        } else if (state.current_step != WizardStep::Welcome) {
            nav.push_back(text(" Next > ") | color(theme.color.accent) | bold);
        } else {
            nav.push_back(text(" Get Started > ") | color(theme.color.accent) | bold);
        }
        nav.push_back(text("     "));
        nav.push_back(text(" Cancel ") | color(theme.color.muted) | dim);

        return vbox({
            step_bar,
            separator(),
            body | flex,
            separator(),
            hbox(std::move(nav)),
        }) | borderRounded | color(theme.color.border) | size(WIDTH, GREATER_THAN, 60);
    }

    bool OnEvent(ftxui::Event event) override {
        using namespace ftxui;

        // Provider step: arrow keys to select
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

        // Security step: arrow keys to select autonomy
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

        // Navigation: Enter = Next/Finish, Escape = Cancel
        if (event == Event::Return) {
            if (state.current_step == WizardStep::Review) {
                sync_state();
                state.finished = true;
                return true;
            }
            go_next();
            return true;
        }

        if (event == Event::Escape) {
            state.cancelled = true;
            return true;
        }

        // Alt+Left = Back
        if (event.input() == "\x1b" "D") {  // Alt+Left
            go_back();
            return true;
        }

        // Forward text input events to active input component
        if (state.current_step == WizardStep::Provider) {
            // No text input on provider step (arrow selection only)
            return false;
        }
        if (state.current_step == WizardStep::ApiKey) {
            return key_input_->OnEvent(event);
        }
        if (state.current_step == WizardStep::Model) {
            return model_input_->OnEvent(event);
        }
        if (state.current_step == WizardStep::Workspace) {
            return workspace_input_->OnEvent(event);
        }

        return false;
    }

    void go_next() {
        int step = static_cast<int>(state.current_step);
        // Skip API Key step for Ollama
        if (step == static_cast<int>(WizardStep::Provider) && selected_provider == 2) {
            step = static_cast<int>(WizardStep::Model);  // Skip ApiKey
        } else {
            step++;
        }
        if (step < static_cast<int>(WizardStep::Count)) {
            state.current_step = static_cast<WizardStep>(step);
        }
    }

    void go_back() {
        int step = static_cast<int>(state.current_step);
        // Skip API Key step for Ollama when going back
        if (step == static_cast<int>(WizardStep::Model) && selected_provider == 2) {
            step = static_cast<int>(WizardStep::Provider);
        } else {
            step--;
        }
        if (step >= 0) {
            state.current_step = static_cast<WizardStep>(step);
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

    // Step renderers

    ftxui::Element render_welcome() {
        using namespace ftxui;
        auto& theme = default_theme();

        std::vector<Element> lines;
        lines.push_back(text(""));
        lines.push_back(text("  ⚕  Embedded Agent Setup Wizard") | color(theme.color.primary) | bold);
        lines.push_back(text(""));
        lines.push_back(text("  Let's configure your Embedded Agent installation.") | color(theme.color.text));
        lines.push_back(text("  Press Ctrl+C at any time to exit.") | color(theme.color.muted) | dim);
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
        return render_provider_step(state, selected_provider);
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

        // Provider section
        lines.push_back(text("  Provider") | color(theme.color.label) | bold);
        lines.push_back(hbox({
            text("    type:       ") | color(theme.color.muted),
            text(config.provider.type) | color(theme.color.text),
        }));
        if (!config.provider.base_url.empty()) {
            lines.push_back(hbox({
                text("    base_url:   ") | color(theme.color.muted),
                text(config.provider.base_url) | color(theme.color.text),
            }));
        }
        if (!config.provider.api_key.empty()) {
            lines.push_back(hbox({
                text("    api_key:    ") | color(theme.color.muted),
                text(config.provider.api_key.substr(0, 8) + "…") | color(theme.color.text),
            }));
        }
        lines.push_back(hbox({
            text("    model:      ") | color(theme.color.muted),
            text(config.agent.model) | color(theme.color.text),
        }));
        lines.push_back(text(""));

        // Security section
        lines.push_back(text("  Security") | color(theme.color.label) | bold);
        lines.push_back(hbox({
            text("    autonomy:   ") | color(theme.color.muted),
            text(config.security.autonomy) | color(theme.color.text),
        }));
        if (!config.security.workspace.empty()) {
            lines.push_back(hbox({
                text("    workspace:  ") | color(theme.color.muted),
                text(config.security.workspace) | color(theme.color.text),
            }));
        }
        lines.push_back(text(""));

        lines.push_back(text("  Press Enter to save configuration") | color(theme.color.accent));

        return vbox(std::move(lines));
    }
};

}  // anonymous namespace

ftxui::Component make_setup_wizard(WizardState& state) {
    return ftxui::Make<WizardImpl>(state);
}

}  // namespace ea::tui
