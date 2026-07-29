#pragma once
#include "base/Result.h"
#include <string>
#include <optional>

namespace ea::agent {

struct ContextFileResult {
    std::string filename;    // Discovered file name (e.g., "AGENTS.md")
    std::string content;     // File content (possibly truncated)
    std::string rel_path;    // Path relative to cwd (or absolute if outside cwd)
};

struct ContextDiscoveryConfig {
    int max_chars = 20000;   // Context file char limit (CONTEXT_FILE_MAX_CHARS)
    std::string cwd;         // Current working directory (empty → use getcwd)
};

class ContextDiscovery {
public:
    explicit ContextDiscovery(ContextDiscoveryConfig config = {});

    // Load SOUL.md from config_dir (~/.embedded-agent/SOUL.md)
    // Returns nullopt if file not found or empty
    std::optional<std::string> load_soul();

    // Discover project context file with mutual-exclusion priority:
    //   1. .ea.md / EA.md  — cwd up to git root (nearest wins)
    //   2. AGENTS.md / agents.md — cwd only
    //   3. CLAUDE.md / claude.md — cwd only
    //   4. .cursorrules — cwd only
    // First found stops the search. Returns nullopt if none found.
    std::optional<ContextFileResult> discover_project_context();

private:
    ContextDiscoveryConfig config_;

    // Priority loaders
    std::optional<ContextFileResult> load_ea_md();
    std::optional<ContextFileResult> load_agents_md();
    std::optional<ContextFileResult> load_claude_md();
    std::optional<ContextFileResult> load_cursorrules();

    // Truncate content: keep head (70%) + tail (20%), insert notice in middle
    std::string truncate(const std::string& content, const std::string& filename);

    // Read and prepare a context file
    std::optional<ContextFileResult> read_context_file(
        const std::string& path, const std::string& display_name);
};

}  // namespace ea::agent
