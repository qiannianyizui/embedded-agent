#include "FactStoreTool.h"

namespace ea::tool {

json FactStoreTool::parameters_schema() const {
    return json::parse(R"delim({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["add", "search", "probe", "related", "reason", "contradict", "update", "remove", "list"]
            },
            "content": {"type": "string", "description": "Fact content (for add/update)"},
            "category": {
                "type": "string",
                "enum": ["user_pref", "project", "tool", "general"],
                "default": "general"
            },
            "tags": {"type": "string", "default": ""},
            "query": {"type": "string", "description": "Search query (for search)"},
            "entity": {"type": "string", "description": "Entity name (for probe/related)"},
            "entities": {
                "type": "array",
                "items": {"type": "string"},
                "description": "Entity names (for reason)"
            },
            "fact_id": {"type": "integer", "description": "Fact ID (for update/remove)"},
            "trust_delta": {"type": "number", "description": "Trust adjustment (for update)"},
            "min_trust": {"type": "number", "default": 0.3},
            "limit": {"type": "integer", "default": 10}
        },
        "required": ["action"]
    })delim");
}

Result<ToolResult> FactStoreTool::execute(const json& args) {
    if (!memory_) {
        return Error::tool_error("Memory backend is not available");
    }
    if (!memory_->is_holographic()) {
        return Error::tool_error("Holographic memory is required for fact operations");
    }

    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }

    std::string action = args["action"].get<std::string>();

    if (action == "add") {
        if (!args.contains("content") || !args["content"].is_string()) {
            return Error::tool_error("Missing 'content' for add action");
        }
        std::string content = args["content"].get<std::string>();
        std::string category = args.value("category", "general");
        std::string tags = args.value("tags", "");

        auto result = memory_->add_fact(content, category, tags);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        // Mirror user-preference facts into USER.md so they survive a DB wipe.
        if (curated_ &&
            (category == "user_pref" || category == "user_profile" ||
             category == "user_info")) {
            curated_->add(memory::MemoryTarget::User, content);
        }
        return ToolResult{"", "Fact added with id: " + std::to_string(result.value()), false};

    } else if (action == "search") {
        if (!args.contains("query") || !args["query"].is_string()) {
            return Error::tool_error("Missing 'query' for search action");
        }
        std::string query = args["query"].get<std::string>();
        std::string category = args.value("category", "");
        double min_trust = args.value("min_trust", 0.3);
        int limit = args.value("limit", 10);

        auto result = memory_->search_facts(query, category, min_trust, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& fe : result.value()) {
            output.push_back({
                {"fact_id", fe.fact_id},
                {"content", fe.content},
                {"category", fe.category},
                {"trust_score", fe.trust_score},
                {"entities", fe.entities}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "probe") {
        if (!args.contains("entity") || !args["entity"].is_string()) {
            return Error::tool_error("Missing 'entity' for probe action");
        }
        std::string entity = args["entity"].get<std::string>();
        std::string category = args.value("category", "");
        int limit = args.value("limit", 10);

        auto result = memory_->probe(entity, category, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& fe : result.value()) {
            output.push_back({
                {"fact_id", fe.fact_id},
                {"content", fe.content},
                {"trust_score", fe.trust_score}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "related") {
        if (!args.contains("entity") || !args["entity"].is_string()) {
            return Error::tool_error("Missing 'entity' for related action");
        }
        std::string entity = args["entity"].get<std::string>();
        std::string category = args.value("category", "");
        int limit = args.value("limit", 10);

        auto result = memory_->related(entity, category, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& fe : result.value()) {
            output.push_back({
                {"fact_id", fe.fact_id},
                {"content", fe.content},
                {"trust_score", fe.trust_score}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "reason") {
        if (!args.contains("entities") || !args["entities"].is_array()) {
            return Error::tool_error("Missing 'entities' array for reason action");
        }
        std::vector<std::string> entities;
        for (const auto& e : args["entities"]) {
            entities.push_back(e.get<std::string>());
        }
        std::string category = args.value("category", "");
        int limit = args.value("limit", 10);

        auto result = memory_->reason(entities, category, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& fe : result.value()) {
            output.push_back({
                {"fact_id", fe.fact_id},
                {"content", fe.content},
                {"trust_score", fe.trust_score}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "contradict") {
        std::string category = args.value("category", "");
        double threshold = args.value("threshold", 0.3);
        int limit = args.value("limit", 10);

        auto result = memory_->contradict(category, threshold, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& cp : result.value()) {
            output.push_back({
                {"fact_a", {{"fact_id", cp.fact_a.fact_id}, {"content", cp.fact_a.content}}},
                {"fact_b", {{"fact_id", cp.fact_b.fact_id}, {"content", cp.fact_b.content}}},
                {"contradiction_score", cp.contradiction_score},
                {"shared_entities", cp.shared_entities}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else if (action == "update") {
        if (!args.contains("fact_id") || !args["fact_id"].is_number_integer()) {
            return Error::tool_error("Missing 'fact_id' for update action");
        }
        int fact_id = args["fact_id"].get<int>();

        const std::string* content_ptr = nullptr;
        std::string content_val;
        if (args.contains("content") && args["content"].is_string()) {
            content_val = args["content"].get<std::string>();
            content_ptr = &content_val;
        }

        const double* trust_ptr = nullptr;
        double trust_val;
        if (args.contains("trust_delta") && args["trust_delta"].is_number()) {
            trust_val = args["trust_delta"].get<double>();
            trust_ptr = &trust_val;
        }

        const std::string* tags_ptr = nullptr;
        std::string tags_val;
        if (args.contains("tags") && args["tags"].is_string()) {
            tags_val = args["tags"].get<std::string>();
            tags_ptr = &tags_val;
        }

        const std::string* category_ptr = nullptr;
        std::string category_val;
        if (args.contains("category") && args["category"].is_string()) {
            category_val = args["category"].get<std::string>();
            category_ptr = &category_val;
        }

        auto result = memory_->update_fact(fact_id, content_ptr, trust_ptr, tags_ptr, category_ptr);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        return ToolResult{"", result.value() ? "Fact updated" : "Fact not found or no changes", false};

    } else if (action == "remove") {
        if (!args.contains("fact_id") || !args["fact_id"].is_number_integer()) {
            return Error::tool_error("Missing 'fact_id' for remove action");
        }
        int fact_id = args["fact_id"].get<int>();

        auto result = memory_->remove_fact(fact_id);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }
        return ToolResult{"", result.value() ? "Fact removed" : "Fact not found", false};

    } else if (action == "list") {
        std::string category = args.value("category", "");
        double min_trust = args.value("min_trust", 0.0);
        int limit = args.value("limit", 50);

        auto result = memory_->list_facts(category, min_trust, limit);
        if (!result.ok()) {
            return ToolResult{"", result.error().message, true};
        }

        json output = json::array();
        for (const auto& fe : result.value()) {
            output.push_back({
                {"fact_id", fe.fact_id},
                {"content", fe.content},
                {"category", fe.category},
                {"trust_score", fe.trust_score},
                {"entities", fe.entities}
            });
        }
        return ToolResult{"", output.dump(2), false};

    } else {
        return Error::tool_error("Unknown action: " + action);
    }
}

}  // namespace ea::tool
