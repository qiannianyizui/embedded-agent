#include "app/SetupWizardRunner.h"
#include "platform/Platform.h"
#include "common/io/Logger.h"
#include "common/io/FileSystem.h"
#include "config/Config.h"
#include "ea/build_config.h"
#include <iostream>

#ifdef EA_ENABLE_TUI
#include "tui/SetupWizard.h"
#include <ftxui/component/screen_interactive.hpp>
#endif

namespace ea::app {

int SetupWizardRunner::run(const SetupArgs& args) {
    auto cfg_dir = ea::fs::config_dir();
    ea::log::init(cfg_dir.ok() ? cfg_dir.value() : "/tmp/.embedded-agent", false);

    if (args.non_interactive) {
        auto cfg = ea::tui::run_non_interactive_setup();
        if (!args.config_path.empty()) cfg.config_path = args.config_path;

        auto save_result = ea::config::save(cfg);
        if (!save_result.ok()) {
            std::cerr << "Error saving config: " << save_result.error().message << std::endl;
            return 1;
        }

        std::cout << "✓ Configuration saved to " << cfg.config_path << std::endl;
        return 0;
    }

#ifdef EA_ENABLE_TUI
    ea::tui::WizardState state;

    std::string cfg_path = args.config_path;
    if (cfg_path.empty()) {
        auto cfg_dir = ea::fs::config_dir();
        if (cfg_dir.ok()) {
            cfg_path = cfg_dir.value() + "/config.toml";
        }
    }
    auto exists_result = ea::fs::exists(cfg_path);
    state.has_existing_config = exists_result.ok() && exists_result.value();

    auto home = ea::platform::home_dir();
    std::string hermes_path = home + "/.hermes";
    auto hm_exists = ea::fs::exists(hermes_path);
    state.has_hermes = hm_exists.ok() && hm_exists.value();

    if (state.has_existing_config && !args.reset) {
        auto cfg_result = ea::config::load(cfg_path);
        if (cfg_result.ok()) {
            auto& cfg = cfg_result.value();
            state.provider_type = cfg.provider.type;
            state.base_url = cfg.provider.base_url;
            state.api_key = cfg.provider.api_key;
            state.default_model = cfg.provider.default_model.empty()
                ? cfg.agent.model : cfg.provider.default_model;
            state.workspace = cfg.security.workspace;
            state.autonomy = cfg.security.autonomy;
        }
    }

    auto wizard = ea::tui::make_setup_wizard(state);
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    state.exit_loop = screen.ExitLoopClosure();
    screen.Loop(wizard);

    if (state.cancelled) {
        std::cout << "Setup cancelled." << std::endl;
        return 0;
    }
    if (!state.finished) {
        return 0;
    }

    auto cfg = ea::tui::build_config_from_wizard(state);
    if (!args.config_path.empty()) cfg.config_path = args.config_path;

    auto save_result = ea::config::save(cfg);
    if (!save_result.ok()) {
        std::cerr << "Error saving config: " << save_result.error().message << std::endl;
        return 1;
    }

    std::cout << "✓ Configuration saved to " << cfg.config_path << std::endl;
    return 0;
#else
    std::cerr << "Setup wizard requires TUI support. "
              << "Rebuild with EA_ENABLE_TUI=ON or use --non-interactive." << std::endl;
    return 1;
#endif
}

}  // namespace ea::app
