#pragma once
#include "tool/ITool.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ea::tool {

class Toolset {
public:
    using CheckFn = std::function<bool()>;

    explicit Toolset(std::string name);

    void add(std::unique_ptr<ITool> tool);
    void add(std::unique_ptr<ITool> tool, CheckFn check);

    std::string name() const { return name_; }

    // Only return specs for tools whose check_fn passes (or that have no check_fn)
    std::vector<ToolSpec> active_specs() const;

    // Execute a tool by name within this toolset
    Result<ToolResult> execute(const std::string& tool_name, const json& args);

    // Check if a tool exists in this toolset (regardless of check_fn)
    bool has_tool(const std::string& tool_name) const;

    // Get raw tool pointer by name (for backward compatibility)
    ITool* find_tool(const std::string& tool_name) const;

private:
    struct Entry {
        std::unique_ptr<ITool> tool;
        CheckFn check;  // nullptr = always available
    };

    Entry* find_entry(const std::string& tool_name);
    const Entry* find_entry(const std::string& tool_name) const;

    std::string name_;
    std::vector<Entry> entries_;
};

}  // namespace ea::tool
