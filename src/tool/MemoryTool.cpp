#include "MemoryTool.h"

namespace ea::tool {

json MemoryTool::parameters_schema() const {
    return json::parse(R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["store", "recall", "forget", "add", "list", "remove"]},
            "content": {"type": "string"},
            "query": {"type": "string"},
            "id": {"type": "string"},
            "target": {"type": "string", "enum": ["db", "user", "memory"], "default": "db"},
            "substring": {"type": "string"},
            "category": {"type": "string", "default": "core"},
            "importance": {"type": "integer", "default": 5}
        },
        "required": ["action"]
    })");
}

Result<ToolResult> MemoryTool::execute(const json& args) {
    if (!memory_) {
        return Error::tool_error("Memory backend is not available");
    }

    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }

    std::string action = args["action"].get<std::string>();
    std::string target = "db";
    if (args.contains("target") && args["target"].is_string()) {
        target = args["target"].get<std::string>();
    }

    if (target == "user" || target == "memory") {
        if (!curated_) {
            return ToolResult{"", "File-based memory is not enabled", true};
        }
        auto mem_target = target == "user"
            ? memory::MemoryTarget::User : memory::MemoryTarget::Memory;

        if (action == "add") {
            if (!args.contains("content") || !args["content"].is_string()) {
                return Error::tool_error("Missing or invalid 'content' parameter for add action");
            }
            std::string content = args["content"].get<std::string>();
            auto r = curated_->add(mem_target, args["content"].get<std::string>());
            if (!r.ok()) return ToolResult{"", r.error().message, true};
            // Mirror the file write into the semantic fact store (Hermes
            // on_memory_write behavior): user profile -> user_pref, notes -> general.
            if (memory_) {
                std::string mirror_category =
                    target == "user" ? "user_pref" : "general";
                memory_->add_fact(content, mirror_category);
            }
            return ToolResult{"", "Saved to " + target + " memory", false};
        }
        if (action == "list") {
            auto r = curated_->entries(mem_target);
            if (!r.ok()) return ToolResult{"", r.error().message, true};
            json out = json::array();
            for (const auto& e : r.value()) out.push_back(e);
            return ToolResult{"", out.dump(2), false};
        }
        if (action == "remove") {
            if (!args.contains("substring") || !args["substring"].is_string()) {
                return Error::tool_error("Missing or invalid 'substring' parameter for remove action");
            }
            auto r = curated_->remove(mem_target, args["substring"].get<std::string>());
            if (!r.ok()) return ToolResult{"", r.error().message, true};
            return ToolResult{"", r.value() ? "Removed" : "Not found", false};
        }
        return ToolResult{"", "Action '" + action + "' is not valid for target='" + target + "'", true};
    }

    if (action == "store") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return Error::tool_error("Missing or invalid 'content' parameter for store action");
        }

        std::string content = args["content"].get<std::string>();
        std::string category = "core";
        if (args.contains("category") && args["category"].is_string()) {
            category = args["category"].get<std::string>();
        }
        int importance = 5;
        if (args.contains("importance") && args["importance"].is_number_integer()) {
            importance = args["importance"].get<int>();
        }

        auto result = memory_->store(content, category, importance);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        return ToolResult{"", "Stored with id: " + result.value(), false};

    } else if (action == "recall") {
        if (!args.contains("query") || !args["query"].is_string()) {
            return Error::tool_error("Missing or invalid 'query' parameter for recall action");
        }

        std::string query = args["query"].get<std::string>();
        auto result = memory_->recall(query);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        const auto& entries = result.value();
        json output = json::array();
        for (const auto& entry : entries) {
            output.push_back({
                {"id", entry.id},
                {"content", entry.content},
                {"category", entry.category},
                {"importance", entry.importance},
                {"created_at", entry.created_at}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "forget") {
        if (!args.contains("id") || !args["id"].is_string()) {
            return Error::tool_error("Missing or invalid 'id' parameter for forget action");
        }

        std::string id = args["id"].get<std::string>();
        auto result = memory_->forget(id);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        if (result.value()) {
            return ToolResult{"", "Forgotten: " + id, false};
        } else {
            return ToolResult{"", "Not found: " + id, false};
        }

    } else {
        return Error::tool_error("Unknown action: " + action);
    }
}

}  // namespace ea::tool
