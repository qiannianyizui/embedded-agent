#pragma once
#include "Types.h"
#include "common/base/Result.h"

namespace ea {

class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual Result<ToolResult> execute(const json& args) = 0;
};

}  // namespace ea
