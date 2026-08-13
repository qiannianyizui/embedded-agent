#pragma once
#include "base/Result.h"
#include <string>
#include <vector>

namespace ea::skill {

struct SkillInfo {
    std::string name;            // Frontmatter name, or directory name fallback
    std::string description;
    std::string version;
    std::string category;        // "general" for top-level skills
    std::string directory;       // Absolute path to the skill directory
    std::string relative_dir;    // e.g. "research/arxiv"
    std::vector<std::string> tags;
    std::vector<std::string> platforms;
};

struct PluginInfo {
    std::string name;
    std::string directory;
};

struct MarketplaceInfo {
    std::string name;
    std::string directory;
};

struct SkillOptions {
    bool template_vars = true;      // Replace ${EA_SKILL_DIR}/${EA_SKILL_NAME}
    bool inline_shell = false;      // Execute !`cmd` snippets in SKILL.md
    int inline_shell_timeout = 10;  // Seconds per inline shell snippet
    std::string user_dir;           // Primary writable root for create()
};

// Discovers and reads SKILL.md files, mirroring the Hermes skills model:
// skills/<category>/<name>/SKILL.md with YAML frontmatter metadata.
class SkillManager {
public:
    SkillManager(std::vector<std::string> roots,
                 std::vector<std::string> disabled = {},
                 SkillOptions options = {},
                 std::string user_dir = "");

    Result<std::vector<SkillInfo>> list() const;

    // Load the full SKILL.md content (or a file inside the skill dir when
    // file_path is set, e.g. "references/api.md").
    Result<std::string> view(const std::string& name,
                             const std::string& file_path = "") const;

    // Render the "<available_skills>" block for the system prompt.
    std::string build_index() const;

    // Mutations used by skill_manage. Cache is invalidated automatically.
    Result<std::string> create(const std::string& name,
                               const std::string& description,
                               const std::string& content,
                               const std::string& category = "general");
    Result<void> update(const std::string& name, const std::string& content);
    Result<void> remove(const std::string& name);
    Result<void> disable(const std::string& name);
    Result<void> enable(const std::string& name);

    bool enabled() const { return !roots_.empty(); }

private:
    Result<std::vector<SkillInfo>> scan_impl(bool filter_disabled = true) const;
    Result<std::vector<SkillInfo>> scan_cached() const;
    Result<SkillInfo> find_skill(const std::string& name) const;
    std::string build_cache_key() const;
    void invalidate_cache() const;
    std::string primary_dir() const;

    std::vector<std::string> roots_;
    std::vector<std::string> disabled_;
    SkillOptions options_;
    std::string user_dir_;

    mutable std::vector<SkillInfo> cache_;
    mutable std::string cache_key_;
    mutable bool cache_valid_ = false;
};

// Installs plugins from git URLs or a marketplace JSON (config dir).
// Each plugin must expose skills/; those dirs are added as SkillManager
// roots on the next startup.
class PluginManager {
public:
    PluginManager(std::string plugins_dir, std::string marketplace_path,
                  std::vector<std::string> mirrors = {});

    // source = git URL or name from the marketplace JSON.
    Result<std::string> install(const std::string& source);
    Result<std::vector<PluginInfo>> list() const;
    Result<void> remove(const std::string& name);
    Result<std::vector<std::string>> skill_roots() const;
    Result<void> ensure_marketplace() const;
    // source = owner/repo or git URL; registers a Claude Code style marketplace.
    Result<std::string> add_marketplace(const std::string& source);
    Result<std::vector<MarketplaceInfo>> list_marketplaces() const;
    Result<void> remove_marketplace(const std::string& name);

private:
    Result<std::string> resolve_url(const std::string& source) const;
    Result<std::string> find_marketplace_file(const std::string& name) const;
    std::string marketplaces_dir() const;
    Result<void> clone_with_fallback(const std::string& url,
                                     const std::string& target) const;
    std::vector<std::string> clone_candidates(const std::string& url) const;
    bool probe_reachable(const std::string& url) const;

    std::string plugins_dir_;
    std::string marketplace_path_;
    std::vector<std::string> mirrors_;
};

}  // namespace ea::skill
