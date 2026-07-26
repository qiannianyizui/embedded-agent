// tests/common/MockTool.h
#pragma once
#include "tool/ITool.h"
#include <functional>
#include <vector>
#include <chrono>
#include <thread>

namespace ea::test {

class MockTool : public ITool {
public:
    explicit MockTool(
        std::string name,
        std::string output = "ok",
        bool mutating = true,
        bool dangerous = false
    );

    void set_next_result(Result<ToolResult> result);
    void set_execute_handler(std::function<Result<ToolResult>(const json&)> handler);

    int call_count() const { return call_count_; }
    const std::vector<json>& call_arguments() const { return call_args_; }
    json last_argument() const;

    std::string name() const override;
    std::string description() const override;
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override;
    bool is_dangerous() const override;

private:
    std::string name_;
    std::string default_output_;
    bool mutating_;
    bool dangerous_;
    std::function<Result<ToolResult>(const json&)> handler_;
    int call_count_ = 0;
    std::vector<json> call_args_;
};

class CountingTool : public MockTool {
public:
    explicit CountingTool(std::string name, std::string output = "ok")
        : MockTool(std::move(name), std::move(output)) {}
};

class ErrorTool : public MockTool {
public:
    explicit ErrorTool(std::string name = "error_tool")
        : MockTool(std::move(name)) {
        set_execute_handler([](const json&) -> Result<ToolResult> {
            return Error::tool_error("tool execution failed");
        });
    }
};

class SlowTool : public MockTool {
public:
    SlowTool(std::string name, std::chrono::milliseconds delay)
        : MockTool(std::move(name)), delay_(delay) {
        set_execute_handler([this](const json&) -> Result<ToolResult> {
            std::this_thread::sleep_for(delay_);
            return ToolResult{"slow_1", "delayed result", false};
        });
    }
private:
    std::chrono::milliseconds delay_;
};

}  // namespace ea::test
