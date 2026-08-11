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

SystemPromptParts build_system_prompt_parts(const PromptContext& ctx) {
    SystemPromptParts parts;

    // ── Stable tier ──
    std::ostringstream stable;
    stable << "# Identity\n";
    if (!ctx.soul.empty()) stable << ctx.soul << "\n\n";

    if (!ctx.tool_guidance.empty()) {
        stable << "# Tool Guidance\n" << ctx.tool_guidance << "\n\n";
    }

    if (!ctx.platform_info.empty()) {
        stable << "# Environment\n" << ctx.platform_info << "\n\n";
    }

    if (!ctx.skills_index.empty()) {
        stable << ctx.skills_index << "\n\n";
    }
    parts.stable = stable.str();

    // ── Context tier ──
    if (!ctx.context_files.empty()) {
        parts.context = "# Project Context\n\n" + ctx.context_files + "\n\n";
    }

    // ── Volatile tier ──
    std::ostringstream vol;
    if (!ctx.relevant_memories.empty()) {
        vol << "# Relevant Memories\n";
        for (const auto& mem : ctx.relevant_memories) {
            vol << "- [" << mem.category << "] " << mem.content << "\n";
        }
        vol << "\n";
    }

    if (!ctx.curated_memory.empty()) {
        vol << "# Persistent Memory\n" << ctx.curated_memory << "\n\n";
    }

    if (!ctx.user_profile.empty()) {
        vol << "# User Profile\n" << ctx.user_profile << "\n\n";
    }

    vol << "Current time: " << current_timestamp() << "\n";
    parts.volatile_ = vol.str();

    return parts;
}

std::string build_system_prompt(const PromptContext& ctx) {
    auto parts = build_system_prompt_parts(ctx);
    std::string result;
    if (!parts.stable.empty()) result += parts.stable;
    if (!parts.context.empty()) {
        if (!result.empty()) result += "\n";
        result += parts.context;
    }
    if (!parts.volatile_.empty()) {
        if (!result.empty()) result += "\n";
        result += parts.volatile_;
    }
    return result;
}

}  // namespace ea::agent
