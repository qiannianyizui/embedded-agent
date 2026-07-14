#pragma once
#include "core/ITool.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace ea::tool {

class ToolRegistry {
public:
    void register_tool(std::unique_ptr<ITool> tool);
    ITool* find(const std::string& name) const;
    std::vector<ToolSpec> get_all_specs() const;
    std::vector<std::string> get_all_names() const;
    Result<ToolResult> execute(const std::string& name, const json& args) const;

private:
    std::unordered_map<std::string, std::unique_ptr<ITool>> tools_;
};

}  // namespace ea::tool
