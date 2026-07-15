#pragma once
#include "core/ITool.h"
#include "Toolset.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ea::tool {

class ToolRegistry {
public:
    // Register a toolset
    void register_toolset(std::unique_ptr<Toolset> set);

    // Activate/deactivate toolsets by name
    void activate(const std::string& set_name);
    void deactivate(const std::string& set_name);

    // Only return specs from activated toolsets whose check passes
    std::vector<ToolSpec> active_specs() const;

    // Execute a tool by name, searching across all activated toolsets
    Result<ToolResult> execute(const std::string& tool_name, const json& args) const;

    // Backward compatibility: register a single tool (creates "default" toolset)
    void register_tool(std::unique_ptr<ITool> tool);

    // Find a tool by name across all toolsets
    ITool* find(const std::string& tool_name) const;

    // Get all tool names from activated toolsets
    std::vector<std::string> get_all_names() const;

    // Legacy alias
    std::vector<ToolSpec> get_all_specs() const { return active_specs(); }

private:
    std::map<std::string, std::unique_ptr<Toolset>> toolsets_;
    std::set<std::string> active_sets_;
};

}  // namespace ea::tool
