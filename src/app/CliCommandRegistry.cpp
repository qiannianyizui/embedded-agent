#include "app/CliCommandRegistry.h"
#include <iostream>

namespace ea::app {

void CliCommandRegistry::register_command(CliCommand cmd) {
    commands_.push_back(std::move(cmd));
}

bool CliCommandRegistry::parse_command(const std::string& input,
                                        std::string& name,
                                        std::string& args) {
    if (input.empty() || input[0] != '/') return false;

    auto space_pos = input.find(' ');
    if (space_pos == std::string::npos) {
        name = input.substr(1);  // strip leading '/'
        args = "";
    } else {
        name = input.substr(1, space_pos - 1);
        args = input.substr(space_pos + 1);
    }
    return !name.empty();
}

bool CliCommandRegistry::try_dispatch(const std::string& input) const {
    std::string name, args;
    if (!parse_command(input, name, args)) return false;

    for (const auto& cmd : commands_) {
        if (cmd.name == name) {
            cmd.execute(args);
            return true;
        }
    }
    return false;
}

void CliCommandRegistry::print_help() const {
    std::cout << "Available commands:" << std::endl;
    for (const auto& cmd : commands_) {
        std::cout << "  " << cmd.usage << "  —  " << cmd.description << std::endl;
    }
}

}  // namespace ea::app
