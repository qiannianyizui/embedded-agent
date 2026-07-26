#pragma once
#include "core/Types.h"
#include "common/base/Result.h"

namespace ea {

class ITool {
public:
    virtual ~ITool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;
    virtual Result<ToolResult> execute(const json& args) = 0;

    // Tool metadata for security policy integration
    virtual bool is_mutating() const { return true; }
    virtual bool is_dangerous() const { return false; }
};

}  // namespace ea
