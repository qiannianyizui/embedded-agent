#pragma once
#include "tool/ITool.h"

namespace ea::tool {

class SearchTool : public ITool {
public:
    std::string name() const override { return "search_files"; }
    std::string description() const override { return "Search file contents by pattern"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return false; }
};

}  // namespace ea::tool
