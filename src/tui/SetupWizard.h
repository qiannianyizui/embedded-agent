// SetupWizard — FTXUI multi-step interactive setup wizard
#pragma once
#include "config/Config.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <string>
#include <vector>

namespace ea::tui {

// Wizard step identifiers
enum class WizardStep {
    Welcome = 0,
    Provider = 1,
    ApiKey = 2,
    Model = 3,
    Workspace = 4,
    Security = 5,
    Review = 6,
    Count = 7,
};

// Wizard state — accumulates user input across steps
struct WizardState {
    // Provider settings
    std::string provider_type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string default_model;

    // Workspace
    std::string workspace;

    // Security
    std::string autonomy = "supervised";

    // Wizard navigation
    WizardStep current_step = WizardStep::Welcome;
    bool finished = false;    // User pressed Finish
    bool cancelled = false;   // User cancelled

    // Detection results (set before wizard starts)
    bool has_existing_config = false;
    bool has_openclaw = false;

    // Screen pointer — set before Loop() so Cancel/Finish can call Exit()
    ftxui::ScreenInteractive* screen = nullptr;
};

// Create the SetupWizard FTXUI component
ftxui::Component make_setup_wizard(WizardState& state);

// Build an AppConfig from the wizard state
ea::config::AppConfig build_config_from_wizard(const WizardState& state);

// Run the setup wizard in non-interactive mode (use defaults + env vars)
// Returns the generated AppConfig
ea::config::AppConfig run_non_interactive_setup();

}  // namespace ea::tui
