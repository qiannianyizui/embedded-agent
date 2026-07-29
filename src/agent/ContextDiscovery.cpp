#include "ContextDiscovery.h"
#include "io/FileSystem.h"
#include "log/Logger.h"
#include <unistd.h>

namespace ea::agent {

ContextDiscovery::ContextDiscovery(ContextDiscoveryConfig config)
    : config_(std::move(config)) {}

std::optional<std::string> ContextDiscovery::load_soul() {
    auto cfg_dir = fs::config_dir();
    if (!cfg_dir.ok()) {
        EA_DEBUG("Cannot determine config dir for SOUL.md: {}", cfg_dir.error().message);
        return std::nullopt;
    }

    std::string soul_path = cfg_dir.value() + "/SOUL.md";
    auto ex = fs::exists(soul_path);
    if (!ex.ok() || !ex.value()) {
        return std::nullopt;
    }

    auto content = fs::read_file(soul_path);
    if (!content.ok()) {
        EA_DEBUG("Cannot read SOUL.md from {}: {}", soul_path, content.error().message);
        return std::nullopt;
    }

    std::string text = content.value();
    // Trim whitespace
    auto start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return std::nullopt;
    auto end = text.find_last_not_of(" \t\n\r");
    text = text.substr(start, end - start + 1);

    if (text.empty()) return std::nullopt;

    return truncate(text, "SOUL.md");
}

std::optional<ContextFileResult> ContextDiscovery::discover_project_context() {
    // Priority 1: .ea.md / EA.md (walk up to git root)
    if (auto result = load_ea_md()) return result;

    // Priority 2: AGENTS.md (cwd only)
    if (auto result = load_agents_md()) return result;

    // Priority 3: CLAUDE.md (cwd only)
    if (auto result = load_claude_md()) return result;

    // Priority 4: .cursorrules (cwd only)
    if (auto result = load_cursorrules()) return result;

    return std::nullopt;
}

std::optional<ContextFileResult> ContextDiscovery::load_ea_md() {
    std::string cwd = config_.cwd;
    if (cwd.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) {
            cwd = buf;
        } else {
            return std::nullopt;
        }
    }

    // Find git root for traversal boundary
    auto git_root = fs::find_git_root(cwd);
    std::string stop_at;
    if (git_root.ok()) {
        stop_at = git_root.value();
    }
    // If no git root, walk_up_find only checks cwd (safety measure)

    auto found = fs::walk_up_find(cwd, {".ea.md", "EA.md"}, stop_at);
    if (!found.ok() || found.value().empty()) {
        return std::nullopt;
    }

    std::string path = found.value();
    // Compute display name (relative to cwd, or just filename)
    std::string display = path;
    if (path.size() > cwd.size() && path.substr(0, cwd.size()) == cwd) {
        display = path.substr(cwd.size() + 1);  // skip "/"
    } else {
        // Outside cwd, use just filename
        auto pos = path.rfind('/');
        if (pos != std::string::npos) {
            display = path.substr(pos + 1);
        }
    }

    return read_context_file(path, display);
}

std::optional<ContextFileResult> ContextDiscovery::load_agents_md() {
    std::string cwd = config_.cwd;
    if (cwd.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) cwd = buf;
        else return std::nullopt;
    }

    for (const auto& name : {"AGENTS.md", "agents.md"}) {
        std::string path = cwd + "/" + name;
        auto ex = fs::exists(path);
        if (ex.ok() && ex.value()) {
            return read_context_file(path, name);
        }
    }
    return std::nullopt;
}

std::optional<ContextFileResult> ContextDiscovery::load_claude_md() {
    std::string cwd = config_.cwd;
    if (cwd.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) cwd = buf;
        else return std::nullopt;
    }

    for (const auto& name : {"CLAUDE.md", "claude.md"}) {
        std::string path = cwd + "/" + name;
        auto ex = fs::exists(path);
        if (ex.ok() && ex.value()) {
            return read_context_file(path, name);
        }
    }
    return std::nullopt;
}

std::optional<ContextFileResult> ContextDiscovery::load_cursorrules() {
    std::string cwd = config_.cwd;
    if (cwd.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) cwd = buf;
        else return std::nullopt;
    }

    std::string path = cwd + "/.cursorrules";
    auto ex = fs::exists(path);
    if (!ex.ok() || !ex.value()) {
        return std::nullopt;
    }

    return read_context_file(path, ".cursorrules");
}

std::optional<ContextFileResult> ContextDiscovery::read_context_file(
    const std::string& path, const std::string& display_name) {
    auto content = fs::read_file(path);
    if (!content.ok()) {
        EA_DEBUG("Cannot read context file {}: {}", path, content.error().message);
        return std::nullopt;
    }

    std::string text = content.value();
    // Trim whitespace
    auto start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return std::nullopt;
    auto end = text.find_last_not_of(" \t\n\r");
    text = text.substr(start, end - start + 1);

    if (text.empty()) return std::nullopt;

    ContextFileResult result;
    result.filename = display_name;
    result.content = truncate(text, display_name);
    result.rel_path = path;
    return result;
}

std::string ContextDiscovery::truncate(const std::string& content,
                                        const std::string& filename) {
    if (static_cast<int>(content.size()) <= config_.max_chars) {
        return content;
    }

    // Keep head (70%) + tail (20%), with truncation notice (10%) in between
    int head_len = static_cast<int>(config_.max_chars * 0.7);
    int tail_len = static_cast<int>(config_.max_chars * 0.2);
    int omitted = static_cast<int>(content.size()) - head_len - tail_len;

    std::string notice = "\n... [" + std::to_string(omitted) +
                         " chars truncated from " + filename + "] ...\n";

    return content.substr(0, head_len) + notice +
           content.substr(content.size() - tail_len);
}

}  // namespace ea::agent
