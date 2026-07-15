#include "SystemPrompt.h"
#include <ctime>
#include <sstream>

namespace ea::agent {

static std::string current_timestamp() {
    auto now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

std::string build_system_prompt(const PromptContext& ctx) {
    std::ostringstream ss;

    // Stable layer: identity + tools + platform
    ss << "# Identity\n";
    if (!ctx.soul.empty()) ss << ctx.soul << "\n\n";

    if (!ctx.tool_guidance.empty()) {
        ss << "# Tool Guidance\n" << ctx.tool_guidance << "\n\n";
    }

    if (!ctx.platform_info.empty()) {
        ss << "# Environment\n" << ctx.platform_info << "\n\n";
    }

    // Volatile layer: memories + timestamp
    if (!ctx.relevant_memories.empty()) {
        ss << "# Relevant Memories\n";
        for (const auto& mem : ctx.relevant_memories) {
            ss << "- [" << mem.category << "] " << mem.content << "\n";
        }
        ss << "\n";
    }

    if (!ctx.user_profile.empty()) {
        ss << "# User Profile\n" << ctx.user_profile << "\n\n";
    }

    ss << "Current time: " << current_timestamp() << "\n";

    return ss.str();
}

}  // namespace ea::agent
