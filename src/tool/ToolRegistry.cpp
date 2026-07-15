#include "ToolRegistry.h"

namespace ea::tool {

void ToolRegistry::register_toolset(std::unique_ptr<Toolset> set) {
    std::string name = set->name();
    toolsets_[name] = std::move(set);
    active_sets_.insert(name);
}

void ToolRegistry::activate(const std::string& set_name) {
    active_sets_.insert(set_name);
}

void ToolRegistry::deactivate(const std::string& set_name) {
    active_sets_.erase(set_name);
}

std::vector<ToolSpec> ToolRegistry::active_specs() const {
    std::vector<ToolSpec> specs;
    for (const auto& name : active_sets_) {
        auto it = toolsets_.find(name);
        if (it != toolsets_.end()) {
            auto set_specs = it->second->active_specs();
            specs.insert(specs.end(), set_specs.begin(), set_specs.end());
        }
    }
    return specs;
}

Result<ToolResult> ToolRegistry::execute(const std::string& tool_name, const json& args) const {
    for (const auto& name : active_sets_) {
        auto it = toolsets_.find(name);
        if (it != toolsets_.end() && it->second->has_tool(tool_name)) {
            return it->second->execute(tool_name, args);
        }
    }
    return Error::tool_error("Tool not found: " + tool_name);
}

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    if (toolsets_.find("default") == toolsets_.end()) {
        auto set = std::make_unique<Toolset>("default");
        active_sets_.insert("default");
        toolsets_["default"] = std::move(set);
    }
    toolsets_["default"]->add(std::move(tool));
}

ITool* ToolRegistry::find(const std::string& tool_name) const {
    for (const auto& [name, set] : toolsets_) {
        auto* tool = set->find_tool(tool_name);
        if (tool) return tool;
    }
    return nullptr;
}

std::vector<std::string> ToolRegistry::get_all_names() const {
    std::vector<std::string> names;
    for (const auto& name : active_sets_) {
        auto it = toolsets_.find(name);
        if (it != toolsets_.end()) {
            auto specs = it->second->active_specs();
            for (const auto& spec : specs) {
                names.push_back(spec.name);
            }
        }
    }
    return names;
}

}  // namespace ea::tool
