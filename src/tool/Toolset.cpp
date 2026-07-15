#include "Toolset.h"

namespace ea::tool {

Toolset::Toolset(std::string name) : name_(std::move(name)) {}

void Toolset::add(std::unique_ptr<ITool> tool) {
    Entry entry;
    entry.tool = std::move(tool);
    entry.check = nullptr;
    entries_.push_back(std::move(entry));
}

void Toolset::add(std::unique_ptr<ITool> tool, CheckFn check) {
    Entry entry;
    entry.tool = std::move(tool);
    entry.check = std::move(check);
    entries_.push_back(std::move(entry));
}

std::vector<ToolSpec> Toolset::active_specs() const {
    std::vector<ToolSpec> specs;
    for (const auto& entry : entries_) {
        if (!entry.check || entry.check()) {
            specs.push_back({
                entry.tool->name(),
                entry.tool->description(),
                entry.tool->parameters_schema()
            });
        }
    }
    return specs;
}

Result<ToolResult> Toolset::execute(const std::string& tool_name, const json& args) {
    auto* entry = find_entry(tool_name);
    if (!entry) {
        return Error::tool_error("Tool not found in toolset '" + name_ + "': " + tool_name);
    }
    return entry->tool->execute(args);
}

bool Toolset::has_tool(const std::string& tool_name) const {
    return find_entry(tool_name) != nullptr;
}

ITool* Toolset::find_tool(const std::string& tool_name) const {
    auto* entry = find_entry(tool_name);
    return entry ? entry->tool.get() : nullptr;
}

Toolset::Entry* Toolset::find_entry(const std::string& tool_name) {
    for (auto& entry : entries_) {
        if (entry.tool->name() == tool_name) return &entry;
    }
    return nullptr;
}

const Toolset::Entry* Toolset::find_entry(const std::string& tool_name) const {
    for (const auto& entry : entries_) {
        if (entry.tool->name() == tool_name) return &entry;
    }
    return nullptr;
}

}  // namespace ea::tool
