#include "FactFeedbackTool.h"

namespace ea::tool {

json FactFeedbackTool::parameters_schema() const {
    return json::parse(R"delim({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["helpful", "unhelpful"]
            },
            "fact_id": {"type": "integer", "description": "ID of the fact to give feedback on"}
        },
        "required": ["action", "fact_id"]
    })delim");
}

Result<ToolResult> FactFeedbackTool::execute(const json& args) {
    if (!memory_) {
        return Error::tool_error("Memory backend is not available");
    }
    if (!memory_->is_holographic()) {
        return Error::tool_error("Holographic memory is required for fact feedback");
    }

    if (!args.contains("action") || !args["action"].is_string()) {
        return Error::tool_error("Missing or invalid 'action' parameter");
    }
    if (!args.contains("fact_id") || !args["fact_id"].is_number_integer()) {
        return Error::tool_error("Missing or invalid 'fact_id' parameter");
    }

    std::string action = args["action"].get<std::string>();
    int fact_id = args["fact_id"].get<int>();
    bool helpful = (action == "helpful");

    if (action != "helpful" && action != "unhelpful") {
        return Error::tool_error("Unknown action: " + action + ". Use 'helpful' or 'unhelpful'");
    }

    auto result = memory_->record_feedback(fact_id, helpful);
    if (!result.ok()) {
        return ToolResult{"", result.error().message, true};
    }

    const auto& fb = result.value();
    json output = {
        {"fact_id", fb.fact_id},
        {"old_trust", fb.old_trust},
        {"new_trust", fb.new_trust},
        {"helpful_count", fb.helpful_count}
    };

    return ToolResult{"", output.dump(2), false};
}

}  // namespace ea::tool
