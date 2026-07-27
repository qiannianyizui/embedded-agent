#include "SearchTool.h"
#include "io/Process.h"
#include <chrono>

namespace ea::tool {

json SearchTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "pattern": {"type": "string"},
            "path": {"type": "string", "default": "."}
        },
        "required": ["pattern"]
    })");
}

Result<ToolResult> SearchTool::execute(const json& args) {
    if (!args.contains("pattern") || !args["pattern"].is_string()) {
        return Error::tool_error("Missing or invalid 'pattern' parameter");
    }

    std::string pattern = args["pattern"].get<std::string>();
    std::string path = ".";
    if (args.contains("path") && args["path"].is_string()) {
        path = args["path"].get<std::string>();
    }

    // Prefer rg (ripgrep) if available, otherwise fall back to grep
    std::string command;
    if (process::command_exists("rg")) {
        command = "rg -n --no-heading --color never " + pattern + " " + path;
    } else {
        command = "grep -rn --color=never " + pattern + " " + path;
    }

    auto result = process::exec(command, "", std::chrono::seconds(30));
    if (!result.ok()) {
        return ToolResult{"", result.error().message, true};
    }

    const auto& exec_res = result.value();
    if (exec_res.exit_code == 0) {
        return ToolResult{"", exec_res.stdout_output, false};
    } else if (exec_res.exit_code == 1) {
        // grep/rg exit code 1 means no matches found
        return ToolResult{"", "No matches found", false};
    } else {
        // Other error
        std::string output;
        if (!exec_res.stderr_output.empty()) {
            output = exec_res.stderr_output;
        } else if (!exec_res.stdout_output.empty()) {
            output = exec_res.stdout_output;
        } else {
            output = "Search failed with exit code " + std::to_string(exec_res.exit_code);
        }
        return ToolResult{"", output, true};
    }
}

}  // namespace ea::tool
