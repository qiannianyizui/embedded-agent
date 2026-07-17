// src/mcp/McpToolAdapter.h
#pragma once
#include "core/ITool.h"
#include "McpClient.h"
#include <memory>

namespace ea::mcp {

class McpToolAdapter : public ITool {
public:
    McpToolAdapter(std::shared_ptr<McpClient> client, ToolSpec spec, bool dangerous = false)
        : client_(std::move(client)), spec_(std::move(spec)), dangerous_(dangerous) {}

    std::string name() const override { return spec_.name; }
    std::string description() const override { return spec_.description; }
    nlohmann::json parameters_schema() const override { return spec_.parameters; }

    Result<ToolResult> execute(const nlohmann::json& args) override {
        return client_->call_tool(spec_.name, args);
    }

    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return dangerous_; }

private:
    std::shared_ptr<McpClient> client_;
    ToolSpec spec_;
    bool dangerous_;
};

}  // namespace ea::mcp
