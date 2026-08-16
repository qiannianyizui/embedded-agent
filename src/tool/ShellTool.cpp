#include "ShellTool.h"
#include "io/Process.h"
#include <chrono>

namespace ea::tool {

json ShellTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "command": {"type": "string"},
            "timeout": {"type": "integer", "default": 30}
        },
        "required": ["command"]
    })");
}

Result<ToolResult> ShellTool::execute(const json& args) {
    if (!args.contains("command") || !args["command"].is_string()) {
        return Error::tool_error("Missing or invalid 'command' parameter");
    }

    std::string command = args["command"].get<std::string>();
    int timeout_sec = 30;
    if (args.contains("timeout") && args["timeout"].is_number_integer()) {
        timeout_sec = args["timeout"].get<int>();
    }

    auto result = process::exec(command, cwd_, std::chrono::seconds(timeout_sec));
    if (!result.ok()) {
        return ToolResult{"", result.error().message, true};
    }

    const auto& exec_res = result.value();
    std::string output;
    if (!exec_res.stdout_output.empty()) {
        output = exec_res.stdout_output;
    }
    if (!exec_res.stderr_output.empty()) {
        if (!output.empty()) output += "\n";
        output += exec_res.stderr_output;
    }

    bool is_error = (exec_res.exit_code != 0);
    if (is_error && output.empty()) {
        output = "Process exited with code " + std::to_string(exec_res.exit_code);
    }

    return ToolResult{"", output, is_error};
}

}  // namespace ea::tool
