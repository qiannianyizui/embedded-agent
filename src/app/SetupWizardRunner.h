// SetupWizardRunner — setup subcommand entry point
#pragma once
#include <string>

namespace ea::app {

struct SetupArgs {
    bool non_interactive = false;
    bool reset = false;
    std::string config_path;
};

class SetupWizardRunner {
public:
    static int run(const SetupArgs& args);
};

}  // namespace ea::app
