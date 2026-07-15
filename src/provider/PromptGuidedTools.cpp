#include "PromptGuidedTools.h"
#include <sstream>

namespace ea::provider {

std::string inject_tools_into_prompt(
    const std::string& system_prompt,
    const std::vector<ToolSpec>& tools)
{
    if (tools.empty()) return system_prompt;

    std::ostringstream ss;
    ss << system_prompt << "\n\n# Available Tools\n\n";
    ss << "You have access to the following tools. To use a tool, "
       << "respond with a JSON object containing \"name\" and \"arguments\" fields.\n\n";

    for (const auto& tool : tools) {
        ss << "\n\n## " << tool.name << "\n";
        ss << tool.description << "\n";
        ss << "Parameters: " << tool.parameters.dump(2) << "\n";
    }

    return ss.str();
}

}  // namespace ea::provider
