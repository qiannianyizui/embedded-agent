#include "WebTool.h"
#include "common/net/HttpClient.h"

namespace ea::tool {

json WebTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["search", "fetch"]},
            "query": {"type": "string"},
            "url": {"type": "string"}
        },
        "required": ["action"]
    })");
}

Result<ToolResult> WebTool::execute(const json& args) {
    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }

    std::string action = args["action"].get<std::string>();

    if (action == "search") {
        return ToolResult{"", "Web search is not yet implemented", false};

    } else if (action == "fetch") {
        if (!args.contains("url") || !args["url"].is_string()) {
            return Error::tool_error("Missing or invalid 'url' parameter for fetch action");
        }

        std::string url = args["url"].get<std::string>();
        net::HttpClient client;
        net::RequestOptions opts;
        opts.timeout = std::chrono::seconds(30);

        auto result = client.get(url, opts);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        const auto& response = result.value();
        if (response.status >= 400) {
            return ToolResult{"",
                "HTTP error " + std::to_string(response.status) + ": " + response.body,
                true};
        }

        return ToolResult{"", response.body, false};

    } else {
        return Error::tool_error("Unknown action: " + action);
    }
}

}  // namespace ea::tool
