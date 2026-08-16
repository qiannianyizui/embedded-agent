// SetupWizard — FTXUI multi-step interactive setup wizard
#pragma once
#include "config/Config.h"
#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ea::tui {

// Wizard step identifiers
enum class WizardStep {
    Welcome = 0,
    Provider = 1,
    ApiKey = 2,
    Model = 3,
    Security = 4,
    Review = 5,
    Count = 6,
};

// Wizard state — accumulates user input across steps
struct WizardState {
    // Provider settings
    std::string provider_type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string default_model;

    // Security
    std::string autonomy = "supervised";

    // Wizard navigation
    WizardStep current_step = WizardStep::Welcome;
    bool finished = false;    // User pressed Finish
    bool cancelled = false;   // User cancelled

    // Detection results (set before wizard starts)
    bool has_existing_config = false;
    bool has_hermes = false;

    // Exit callback — set to screen.ExitLoopClosure() before Loop()
    // so Cancel/Finish can exit the event loop.
    std::function<void()> exit_loop;
};

// Create the SetupWizard FTXUI component
ftxui::Component make_setup_wizard(WizardState& state);

// Build an AppConfig from the wizard state
ea::config::AppConfig build_config_from_wizard(const WizardState& state);

// Run the setup wizard in non-interactive mode (use defaults + env vars)
// Returns the generated AppConfig
ea::config::AppConfig run_non_interactive_setup();

}  // namespace ea::tui
