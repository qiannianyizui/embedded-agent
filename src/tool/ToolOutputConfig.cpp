#include "ToolOutputConfig.h"

namespace ea::tool {

size_t ToolOutputConfig::get_limit(const std::string& tool_name) const {
    auto it = per_tool.find(tool_name);
    if (it != per_tool.end()) return it->second;
    return max_bytes;
}

std::string truncate_output(const std::string& output, size_t max_bytes,
                            const std::string& marker)
{
    if (output.size() <= max_bytes) return output;

    size_t content_len = max_bytes > marker.size()
        ? max_bytes - marker.size()
        : 0;
    return output.substr(0, content_len) + marker;
}

}  // namespace ea::tool
