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

}  // namespace ea::skill
