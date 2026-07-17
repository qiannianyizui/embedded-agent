// src/mcp/McpClient.h
#pragma once
#include "ITransport.h"
#include "core/Types.h"
#include "common/base/Result.h"
#include <memory>
#include <string>
#include <vector>

namespace ea::mcp {

class McpClient {
public:
    explicit McpClient(std::unique_ptr<ITransport> transport);

    Result<void> connect();
    Result<void> disconnect();

    Result<std::vector<ToolSpec>> list_tools();
    Result<ToolResult> call_tool(const std::string& name, const nlohmann::json& arguments);

    bool is_connected() const;

private:
    Result<nlohmann::json> send_request(const std::string& method,
                                         const nlohmann::json& params);

    std::unique_ptr<ITransport> transport_;
    bool connected_ = false;
    int next_id_ = 1;
};

}  // namespace ea::mcp
