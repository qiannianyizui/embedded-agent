#pragma once
#include "core/ITool.h"

namespace ea::tool {

class FileTool : public ITool {
public:
    std::string name() const override { return "file"; }
    std::string description() const override { return "File read, write, and edit operations"; }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_dangerous() const override { return true; }
};

}  // namespace ea::tool
