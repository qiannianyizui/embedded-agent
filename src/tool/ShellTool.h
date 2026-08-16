#pragma once
#include "tool/ITool.h"

namespace ea::tool {

class ShellTool : public ITool {
public:
    explicit ShellTool(std::string cwd = {}) : cwd_(std::move(cwd)) {}

    std::string name() const override { return "shell"; }
    std::string description() const override { return "Execute shell commands"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_dangerous() const override { return true; }

private:
    std::string cwd_;
};

}  // namespace ea::tool
