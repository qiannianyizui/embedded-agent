#pragma once
#include <string>
#include <map>

namespace ea::tool {

struct ToolOutputConfig {
    size_t max_bytes = 65536;
    std::map<std::string, size_t> per_tool;
    std::string truncate_marker = "\n...[truncated]";

    size_t get_limit(const std::string& tool_name) const;
};

std::string truncate_output(const std::string& output, size_t max_bytes,
                            const std::string& marker);

}  // namespace ea::tool
