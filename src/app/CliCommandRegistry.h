// CliCommandRegistry — extensible slash-command dispatcher
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace ea::app {

using CommandFn = std::function<void(const std::string& args)>;

struct CliCommand {
    std::string name;         // e.g. "usage", "cost", "history"
    std::string usage;        // e.g. "/usage [global]"
    std::string description;  // e.g. "Show token usage statistics"
    CommandFn execute;
};

class CliCommandRegistry {
public:
    void register_command(CliCommand cmd);

    // Try to dispatch input as a command. Returns true if handled.
    bool try_dispatch(const std::string& input) const;

    // Print help listing all registered commands
    void print_help() const;

private:
    std::vector<CliCommand> commands_;

    // Parse "/name args" → name + args. Returns false if not a command.
    static bool parse_command(const std::string& input,
                             std::string& name,
                             std::string& args);
};

}  // namespace ea::app
