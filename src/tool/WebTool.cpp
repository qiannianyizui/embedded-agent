#include "WebTool.h"
#include "net/HttpClient.h"

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
        net::RequestOptions opts;
        opts.timeout = std::chrono::seconds(30);

        auto get_result = client_
            ? client_->get(url, opts)
            : net::HttpClient().get(url, opts);

        if (!get_result.ok()) {
            return ToolResult{"", get_result.error().message, true};
        }

        const auto& response = get_result.value();
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
