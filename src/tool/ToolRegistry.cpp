#include "ToolRegistry.h"

namespace ea::tool {

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    std::string name = tool->name();
    tools_[std::move(name)] = std::move(tool);
}

ITool* ToolRegistry::find(const std::string& name) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<ToolSpec> ToolRegistry::get_all_specs() const {
    std::vector<ToolSpec> specs;
    specs.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        specs.push_back(ToolSpec{
            tool->name(),
            tool->description(),
            tool->parameters_schema()
        });
    }
    return specs;
}

std::vector<std::string> ToolRegistry::get_all_names() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        names.push_back(tool->name());
    }
    return names;
}

Result<ToolResult> ToolRegistry::execute(const std::string& name, const json& args) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return Error::not_found("Tool not found: " + name);
    }
    return it->second->execute(args);
}

}  // namespace ea::tool
