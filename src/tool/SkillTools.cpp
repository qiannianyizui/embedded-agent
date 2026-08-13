#include "SkillTools.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ea::tool {

json SkillsListTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "category": {"type": "string"},
            "query": {"type": "string"}
        },
        "required": []
    })");
}

Result<ToolResult> SkillsListTool::execute(const json& args) {
    if (!skills_) return Error::tool_error("Skills system is not available");

    std::string category;
    std::string query;
    if (args.contains("category") && args["category"].is_string()) {
        category = args["category"].get<std::string>();
    }
    if (args.contains("query") && args["query"].is_string()) {
        query = args["query"].get<std::string>();
    }
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto skills = skills_->list();
    if (!skills.ok()) return Error::tool_error(skills.error().message);

    std::ostringstream oss;
    std::string last_category;
    int count = 0;
    for (const auto& info : skills.value()) {
        if (!category.empty() && info.category != category) continue;
        if (!query.empty()) {
            auto haystack = info.name + " " + info.description;
            for (const auto& tag : info.tags) haystack += " " + tag;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (haystack.find(query) == std::string::npos) continue;
        }
        if (info.category != last_category) {
            last_category = info.category;
            oss << "  " << info.category << ":\n";
        }
        oss << "    - " << info.name;
        if (!info.description.empty()) oss << ": " << info.description;
        oss << "\n";
        ++count;
    }

    if (count == 0) {
        return ToolResult{"", "No skills found.", false};
    }
    return ToolResult{"", "<skills>\n" + oss.str() + "</skills>", false};
}

json SkillViewTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "name": {"type": "string"},
            "file_path": {"type": "string"}
        },
        "required": ["name"]
    })");
}

Result<ToolResult> SkillViewTool::execute(const json& args) {
    if (!skills_) return Error::tool_error("Skills system is not available");
    if (!args.contains("name") || !args["name"].is_string()) {
        return Error::tool_error("Missing or invalid 'name' parameter");
    }

    std::string name = args["name"].get<std::string>();
    std::string file_path;
    if (args.contains("file_path") && args["file_path"].is_string()) {
        file_path = args["file_path"].get<std::string>();
    }

    auto content = skills_->view(name, file_path);
    if (!content.ok()) {
        return ToolResult{"", content.error().message, true};
    }
    return ToolResult{"", content.value(), false};
}

json SkillManageTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["create", "update", "delete", "disable", "enable"]},
            "name": {"type": "string"},
            "description": {"type": "string"},
            "content": {"type": "string"},
            "category": {"type": "string"}
        },
        "required": ["action", "name"]
    })");
}

Result<ToolResult> SkillManageTool::execute(const json& args) {
    if (!skills_) return Error::tool_error("Skills system is not available");
    if (!args.contains("action") || !args["action"].is_string() ||
        !args.contains("name") || !args["name"].is_string()) {
        return Error::tool_error("Missing or invalid 'action'/'name' parameters");
    }

    std::string action = args["action"].get<std::string>();
    std::string name = args["name"].get<std::string>();
    std::string description;
    std::string content;
    std::string category;
    if (args.contains("description") && args["description"].is_string()) {
        description = args["description"].get<std::string>();
    }
    if (args.contains("content") && args["content"].is_string()) {
        content = args["content"].get<std::string>();
    }
    if (args.contains("category") && args["category"].is_string()) {
        category = args["category"].get<std::string>();
    }

    if (action == "create") {
        if (content.empty()) {
            return Error::tool_error("'content' is required for create");
        }
        auto result = skills_->create(name, description, content, category);
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Created skill: " + result.value(), false};
    }
    if (action == "update") {
        if (content.empty()) {
            return Error::tool_error("'content' is required for update");
        }
        auto result = skills_->update(name, content);
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Updated skill: " + name, false};
    }
    if (action == "delete") {
        auto result = skills_->remove(name);
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Deleted skill: " + name, false};
    }
    if (action == "disable") {
        auto result = skills_->disable(name);
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Disabled skill: " + name, false};
    }
    if (action == "enable") {
        auto result = skills_->enable(name);
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Enabled skill: " + name, false};
    }

    return Error::tool_error("Unknown skill_manage action: " + action);
}

json PluginInstallTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "source": {"type": "string"}
        },
        "required": ["source"]
    })");
}

Result<ToolResult> PluginInstallTool::execute(const json& args) {
    if (!plugins_) return Error::tool_error("Plugin system is not available");
    if (!args.contains("source") || !args["source"].is_string()) {
        return Error::tool_error("Missing or invalid 'source' parameter");
    }

    auto result = plugins_->install(args["source"].get<std::string>());
    if (!result.ok()) {
        return ToolResult{"", result.error().message, true};
    }
    return ToolResult{
        "",
        "Installed plugin: " + result.value() +
            "\nRestart the agent to activate its skills.",
        false};
}

json PluginMarketplaceTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["add", "list", "remove"]},
            "source": {"type": "string"},
            "name": {"type": "string"}
        },
        "required": ["action"]
    })");
}

Result<ToolResult> PluginMarketplaceTool::execute(const json& args) {
    if (!plugins_) return Error::tool_error("Plugin system is not available");
    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }

    auto action = args["action"].get<std::string>();
    if (action == "add") {
        if (!args.contains("source") || !args["source"].is_string()) {
            return Error::tool_error("Missing or invalid 'source' parameter");
        }
        auto result = plugins_->add_marketplace(args["source"].get<std::string>());
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Registered marketplace: " + result.value(), false};
    }
    if (action == "list") {
        auto result = plugins_->list_marketplaces();
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        if (result.value().empty()) {
            return ToolResult{"", "No marketplaces registered.", false};
        }
        std::ostringstream oss;
        oss << "Registered marketplaces:\n";
        for (const auto& m : result.value()) {
            oss << "  - " << m.name << "\n";
        }
        return ToolResult{"", oss.str(), false};
    }
    if (action == "remove") {
        if (!args.contains("name") || !args["name"].is_string()) {
            return Error::tool_error("Missing or invalid 'name' parameter");
        }
        auto result = plugins_->remove_marketplace(args["name"].get<std::string>());
        if (!result.ok()) return ToolResult{"", result.error().message, true};
        return ToolResult{"", "Removed marketplace: " + args["name"].get<std::string>(), false};
    }
    return Error::tool_error("Unknown plugin_marketplace action: " + action);
}

}  // namespace ea::tool
