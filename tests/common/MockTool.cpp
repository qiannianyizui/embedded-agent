// tests/common/MockTool.cpp
#include "MockTool.h"

namespace ea::test {

MockTool::MockTool(std::string name, std::string output, bool mutating, bool dangerous)
    : name_(std::move(name)), default_output_(std::move(output)),
      mutating_(mutating), dangerous_(dangerous) {}

void MockTool::set_next_result(Result<ToolResult> result) {
    handler_ = [r = std::move(result)](const json&) mutable -> Result<ToolResult> {
        return std::move(r);
    };
}

void MockTool::set_execute_handler(std::function<Result<ToolResult>(const json&)> handler) {
    handler_ = std::move(handler);
}

json MockTool::last_argument() const {
    return call_args_.empty() ? json() : call_args_.back();
}

std::string MockTool::name() const { return name_; }
std::string MockTool::description() const { return "mock tool: " + name_; }

json MockTool::parameters_schema() const {
    return json::object();
}

Result<ToolResult> MockTool::execute(const json& args) {
    call_count_++;
    call_args_.push_back(args);
    if (handler_) {
        return handler_(args);
    }
    return ToolResult{name_ + "_result", default_output_, false};
}

bool MockTool::is_mutating() const { return mutating_; }
bool MockTool::is_dangerous() const { return dangerous_; }

}  // namespace ea::test
