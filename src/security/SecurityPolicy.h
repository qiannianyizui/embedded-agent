#pragma once
#include "base/Result.h"
#include <string>
#include <vector>

namespace ea::security {

enum class AutonomyLevel { ReadOnly, Supervised, Full };

class SecurityPolicy {
public:
    explicit SecurityPolicy(AutonomyLevel level = AutonomyLevel::Supervised);

    Result<void> check_command(const std::string& command) const;
    Result<void> check_file_path(const std::string& path) const;
    Result<void> check_tool(const std::string& tool_name) const;

    void set_workspace(const std::string& path);
    void set_allowed_commands(const std::vector<std::string>& cmds);

    AutonomyLevel level() const { return level_; }

private:
    AutonomyLevel level_;
    std::string workspace_;
    std::vector<std::string> allowed_commands_;

    static const std::vector<std::string> DANGEROUS_COMMANDS;
    static const std::vector<std::string> READONLY_TOOLS;
};

}  // namespace ea::security
