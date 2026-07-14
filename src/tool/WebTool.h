#pragma once
#include "core/ITool.h"

namespace ea::tool {

class WebTool : public ITool {
public:
    std::string name() const override { return "web"; }
    std::string description() const override { return "Web search and fetch operations"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
};

}  // namespace ea::tool
