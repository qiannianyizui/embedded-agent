// src/mcp/McpClient.cpp
#include "McpClient.h"
#include "common/io/Logger.h"

namespace ea::mcp {

McpClient::McpClient(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

Result<void> McpClient::connect() {
    if (connected_) return {};

    auto start_result = transport_->start();
    if (!start_result.ok()) return start_result;

    nlohmann::json params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", {{"name", "embedded-agent"}, {"version", "0.1.0"}}}
    };

    auto init_result = send_request("initialize", params);
    if (!init_result.ok()) {
        transport_->stop();
        return init_result;
    }

    auto& response = init_result.value();
    if (response.contains("protocolVersion")) {
        EA_INFO("MCP server protocol version: {}", response["protocolVersion"].get<std::string>());
    }

    // Send initialized notification
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    transport_->send(notif);

    connected_ = true;
    return {};
}

Result<void> McpClient::disconnect() {
    if (!connected_) return {};

    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
        {"params", nlohmann::json::object()}
    };
    transport_->send(notif);

    transport_->stop();
    connected_ = false;
    return {};
}

Result<nlohmann::json> McpClient::send_request(const std::string& method,
                                                 const nlohmann::json& params) {
    nlohmann::json request = {
        {"jsonrpc", "2.0"},
        {"id", next_id_++},
        {"method", method}
    };

    if (!params.is_null()) {
        request["params"] = params;
    }

    auto result = transport_->send(request);
    if (!result.ok()) return result;

    auto& response = result.value();
    if (response.contains("error")) {
        auto& error = response["error"];
        std::string msg = error.contains("message") ? error["message"].get<std::string>() : "Unknown error";
        int code = error.contains("code") ? error["code"].get<int>() : -1;
        return Error::net("MCP error " + std::to_string(code) + ": " + msg);
    }

    if (response.contains("result")) {
        return response["result"];
    }

    return Error::parse("MCP response missing 'result' field");
}

Result<std::vector<ToolSpec>> McpClient::list_tools() {
    auto result = send_request("tools/list", nlohmann::json::object());
    if (!result.ok()) return result.error();

    std::vector<ToolSpec> tools;
    auto& data = result.value();

    if (!data.contains("tools") || !data["tools"].is_array()) {
        return Error::parse("MCP tools/list response missing 'tools' array");
    }

    for (const auto& tool : data["tools"]) {
        ToolSpec spec;
        spec.name = tool.value("name", "");
        spec.description = tool.value("description", "");
        spec.parameters = tool.value("inputSchema", nlohmann::json::object());
        if (!spec.name.empty()) {
            tools.push_back(std::move(spec));
        }
    }

    return tools;
}

Result<ToolResult> McpClient::call_tool(const std::string& name,
                                          const nlohmann::json& arguments) {
    nlohmann::json params = {
        {"name", name},
        {"arguments", arguments.is_null() ? nlohmann::json::object() : arguments}
    };

    auto result = send_request("tools/call", params);
    if (!result.ok()) return result.error();

    auto& data = result.value();
    bool is_error = data.value("isError", false);

    std::string output;
    if (data.contains("content") && data["content"].is_array()) {
        for (const auto& item : data["content"]) {
            if (item.value("type", "") == "text") {
                if (!output.empty()) output += "\n";
                output += item.value("text", "");
            }
        }
    } else if (data.contains("content") && data["content"].is_string()) {
        output = data["content"].get<std::string>();
    }

    if (output.empty() && is_error) {
        output = "MCP tool error (no message)";
    }

    return ToolResult{"", output, is_error};
}

bool McpClient::is_connected() const {
    return connected_;
}

}  // namespace ea::mcp
