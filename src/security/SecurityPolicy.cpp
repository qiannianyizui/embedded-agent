#include "SecurityPolicy.h"
#include "base/StringUtil.h"

namespace ea::security {

const std::vector<std::string> SecurityPolicy::DANGEROUS_COMMANDS = {
    "rm -rf /", "mkfs", "dd if=", ":(){:|:&};:", "fork bomb",
    "shutdown", "reboot", "init 0", "init 6",
    "mv / ", "chmod -R 777 /", "chown -R",
    "> /dev/sda", "curl | sh", "wget | sh",
    "systemctl stop", "service stop"
};

const std::vector<std::string> SecurityPolicy::READONLY_TOOLS = {
    "search_files", "web"
};

SecurityPolicy::SecurityPolicy(AutonomyLevel level) : level_(level) {}

Result<void> SecurityPolicy::check_command(const std::string& command) const {
    if (level_ == AutonomyLevel::Full) return {};

    // Check dangerous commands (always blocked unless Full)
    auto lower_cmd = util::to_lower(command);
    for (const auto& dangerous : DANGEROUS_COMMANDS) {
        if (lower_cmd.find(util::to_lower(dangerous)) != std::string::npos) {
            return Error::security("dangerous command blocked: " + command);
        }
    }

    // In ReadOnly mode, block all commands
    if (level_ == AutonomyLevel::ReadOnly) {
        return Error::security("command blocked in read-only mode: " + command);
    }

    // In Supervised mode, check whitelist if set
    if (!allowed_commands_.empty()) {
        // Extract the base command (first word)
        auto parts = util::split(command, ' ');
        if (!parts.empty()) {
            std::string base_cmd = parts[0];
            // Extract just the binary name from path
            auto slash_pos = base_cmd.rfind('/');
            if (slash_pos != std::string::npos) {
                base_cmd = base_cmd.substr(slash_pos + 1);
            }
            bool found = false;
            for (const auto& allowed : allowed_commands_) {
                if (base_cmd == allowed) { found = true; break; }
            }
            if (!found) {
                return Error::security("command not in whitelist: " + base_cmd);
            }
        }
    }

    return {};
}

Result<void> SecurityPolicy::check_file_path(const std::string& path) const {
    if (level_ == AutonomyLevel::Full) return {};
    if (level_ == AutonomyLevel::ReadOnly) {
        return Error::security("file access blocked in read-only mode: " + path);
    }
    // In Supervised mode, check workspace boundary if set
    if (!workspace_.empty()) {
        if (!util::starts_with(path, workspace_)) {
            return Error::security("path outside workspace: " + path);
        }
    }
    return {};
}

Result<void> SecurityPolicy::check_tool(const std::string& tool_name) const {
    if (level_ == AutonomyLevel::Full) return {};
    if (level_ == AutonomyLevel::ReadOnly) {
        for (const auto& ro_tool : READONLY_TOOLS) {
            if (tool_name == ro_tool) return {};
        }
        return Error::security("tool blocked in read-only mode: " + tool_name);
    }
    return {};
}

void SecurityPolicy::set_workspace(const std::string& path) {
    workspace_ = path;
}

void SecurityPolicy::set_allowed_commands(const std::vector<std::string>& cmds) {
    allowed_commands_ = cmds;
}

}  // namespace ea::security
