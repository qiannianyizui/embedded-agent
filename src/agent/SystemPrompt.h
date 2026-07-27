#pragma once
#include <string>
#include <vector>
#include "base/Types.h"

namespace ea::agent {

struct PromptContext {
    std::string soul;
    std::string platform_info;
    std::string tool_guidance;
    std::string user_profile;
    std::vector<MemoryEntry> relevant_memories;
};

std::string build_system_prompt(const PromptContext& ctx);

}  // namespace ea::agent
