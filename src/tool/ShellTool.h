#pragma once
#include "core/ITool.h"

namespace ea::tool {

class ShellTool : public ITool {
public:
    std::string name() const override { return "shell"; }
    std::string description() const override { return "Execute shell commands"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_dangerous() const override { return true; }
};

}  // namespace ea::tool
