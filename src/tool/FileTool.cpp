#include "FileTool.h"
#include "io/FileSystem.h"

namespace ea::tool {

json FileTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["read", "write", "edit"]},
            "path": {"type": "string"},
            "content": {"type": "string"},
            "old_string": {"type": "string"},
            "new_string": {"type": "string"}
        },
        "required": ["action", "path"]
    })");
}

Result<ToolResult> FileTool::execute(const json& args) {
    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }
    if (!args.contains("path") || !args["path"].is_string()) {
        return Error::tool_error("Missing or invalid 'path' parameter");
    }

    std::string action = args["action"].get<std::string>();
    std::string path = args["path"].get<std::string>();

    if (action == "read") {
        auto result = fs::read_file(path);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        return ToolResult{"", result.value(), false};

    } else if (action == "write") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return Error::tool_error("Missing or invalid 'content' parameter for write action");
        }
        std::string content = args["content"].get<std::string>();
        auto result = fs::write_file(path, content);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        return ToolResult{"", "File written successfully: " + path, false};

    } else if (action == "edit") {
        if (!args.contains("old_string") || !args["old_string"].is_string()) {
            return Error::tool_error("Missing or invalid 'old_string' parameter for edit action");
        }
        if (!args.contains("new_string") || !args["new_string"].is_string()) {
            return Error::tool_error("Missing or invalid 'new_string' parameter for edit action");
        }

        std::string old_string = args["old_string"].get<std::string>();
        std::string new_string = args["new_string"].get<std::string>();

        // Read the file
        auto read_result = fs::read_file(path);
        if (!read_result.ok()) {
            return ToolResult{"", read_result.error().message, true};
        }

        std::string content = read_result.value();
        auto pos = content.find(old_string);
        if (pos == std::string::npos) {
            return ToolResult{"", "old_string not found in file: " + path, true};
        }

        content.replace(pos, old_string.size(), new_string);

        auto write_result = fs::write_file(path, content);
        if (!write_result.ok()) {
            return ToolResult{"", write_result.error().message, true};
        }
        return ToolResult{"", "File edited successfully: " + path, false};

    } else {
        return Error::tool_error("Unknown action: " + action);
    }
}

}  // namespace ea::tool
