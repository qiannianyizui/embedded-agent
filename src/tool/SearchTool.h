#pragma once
#include "core/ITool.h"

namespace ea::tool {

class SearchTool : public ITool {
public:
    std::string name() const override { return "search_files"; }
    std::string description() const override { return "Search file contents by pattern"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
};

}  // namespace ea::tool
