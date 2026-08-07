#pragma once
#include <string>
#include <vector>
#include "base/Types.h"

namespace ea::agent {

struct PromptContext {
    // Stable layer: identity + tools + platform (doesn't change across turns)
    std::string soul;
    std::string tool_guidance;
    std::string platform_info;
    std::string skills_index;

    // Context layer: project context files (changes between projects)
    std::string context_files;

    // Volatile layer: memories + profile + timestamp (may change each turn)
    std::vector<MemoryEntry> relevant_memories;
    std::string user_profile;
};

// Three-layer result for cache-friendly prompt assembly
struct SystemPromptParts {
    std::string stable;     // Identity + Tool Guidance + Environment
    std::string context;    // Project Context
    std::string volatile_;  // Memories + User Profile + Timestamp
};

SystemPromptParts build_system_prompt_parts(const PromptContext& ctx);

// Compatible interface: join three layers with "\n\n"
std::string build_system_prompt(const PromptContext& ctx);

}  // namespace ea::agent
