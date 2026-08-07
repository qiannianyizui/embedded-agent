#include "Skill.h"
#include "io/FileSystem.h"
#include "io/Process.h"
#include "platform/Platform.h"
#include "log/Logger.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace ea::skill {
namespace fs = std::filesystem;
namespace efs = ea::fs;

namespace {

const std::set<std::string> kExcludedDirs = {
    ".git", ".github", ".cache", ".archive", ".hub", ".venv",
    "node_modules", "venv", "site-packages", "__pycache__",
    ".tox", ".nox", ".pytest_cache", ".mypy_cache", ".ruff_cache",
};

const std::set<std::string> kSupportDirs = {
    "references", "templates", "assets", "scripts",
};

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t' ||
                                s[begin] == '\r' || s[begin] == '\n')) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                           s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string unquote(const std::string& s) {
    auto t = trim(s);
    if (t.size() >= 2 &&
        ((t.front() == '"' && t.back() == '"') ||
         (t.front() == '\'' && t.back() == '\''))) {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

std::vector<std::string> parse_list(const std::string& raw) {
    auto t = trim(raw);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
        t = t.substr(1, t.size() - 2);
    }
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= t.size()) {
        auto comma = t.find(',', pos);
        if (comma == std::string::npos) comma = t.size();
        auto item = unquote(t.substr(pos, comma - pos));
        if (!item.empty()) out.push_back(item);
        pos = comma + 1;
    }
    return out;
}

struct Frontmatter {
    std::string name;
    std::string description;
    std::string version;
    std::vector<std::string> platforms;
    std::vector<std::string> tags;
};

Frontmatter parse_frontmatter(const std::string& content) {
    Frontmatter fm;
    if (content.rfind("---", 0) != 0) return fm;

    auto end = content.find("\n---", 3);
    if (end == std::string::npos) return fm;
    std::string yaml = content.substr(3, end - 3);

    std::istringstream ss(yaml);
    std::string line;
    while (std::getline(ss, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto colon = t.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(t.substr(0, colon));
        std::string value = trim(t.substr(colon + 1));
        if (value.empty()) continue;
        if (key == "name") {
            fm.name = unquote(value);
        } else if (key == "description") {
            fm.description = unquote(value);
        } else if (key == "version") {
            fm.version = unquote(value);
        } else if (key == "platforms") {
            fm.platforms = parse_list(value);
        } else if (key == "tags") {
            fm.tags = parse_list(value);
        }
    }
    return fm;
}

std::string join_list(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ", ";
        out += items[i];
    }
    return out;
}

std::string yaml_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string serialize_frontmatter(const Frontmatter& fm) {
    std::ostringstream oss;
    oss << "---\n";
    if (!fm.name.empty()) oss << "name: " << fm.name << "\n";
    if (!fm.description.empty()) {
        oss << "description: \"" << yaml_escape(fm.description) << "\"\n";
    }
    if (!fm.version.empty()) oss << "version: " << fm.version << "\n";
    if (!fm.platforms.empty()) {
        oss << "platforms: [" << join_list(fm.platforms) << "]\n";
    }
    if (!fm.tags.empty()) {
        oss << "tags: [" << join_list(fm.tags) << "]\n";
    }
    oss << "---\n";
    return oss.str();
}

bool safe_relative(const std::string& candidate) {
    if (candidate.empty()) return false;
    if (candidate.front() == '/') return false;
    if (candidate.size() > 1 && candidate[1] == ':') return false;

    size_t pos = 0;
    while (pos <= candidate.size()) {
        auto slash = candidate.find('/', pos);
        if (slash == std::string::npos) slash = candidate.size();
        auto part = candidate.substr(pos, slash - pos);
        if (part.empty() || part == "." || part == "..") return false;
        pos = slash + 1;
    }
    return true;
}

bool valid_skill_name(const std::string& name) {
    if (name.empty() || !safe_relative(name) || name.find('/') != std::string::npos) {
        return false;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

bool platform_matches(const std::vector<std::string>& platforms) {
    if (platforms.empty()) return true;
    auto kind = platform::detect();
    for (const auto& p : platforms) {
        auto name = p;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "linux" && (kind == platform::PlatformKind::Linux ||
                                kind == platform::PlatformKind::Wsl ||
                                kind == platform::PlatformKind::Android)) {
            return true;
        }
        if (name == "android" && kind == platform::PlatformKind::Android) return true;
        if (name == "wsl" && kind == platform::PlatformKind::Wsl) return true;
        if (name == "all" || name == "any") return true;
    }
    return false;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool is_excluded_dir(const std::string& name) {
    return kExcludedDirs.count(name) > 0 ||
           (!name.empty() && name.front() == '.');
}

bool is_support_dir_under_skill(const fs::path& dir) {
    if (kSupportDirs.count(dir.filename().string()) == 0) return false;
    std::error_code ec;
    return fs::exists(dir.parent_path() / "SKILL.md", ec);
}

void collect_skill_files(const std::string& root,
                         std::vector<fs::path>& out) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, ec), end;
    if (ec) return;

    while (it != end) {
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            const auto name = entry.path().filename().string();
            if (is_excluded_dir(name) || is_support_dir_under_skill(entry.path())) {
                it.disable_recursion_pending();
            }
            ++it;
            continue;
        }
        if (entry.is_regular_file(ec) &&
            entry.path().filename() == "SKILL.md") {
            out.push_back(entry.path());
        }
        ++it;
    }
}

std::string replace_all(std::string text,
                        const std::string& from,
                        const std::string& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string expand_inline_shell(const std::string& content,
                                const std::string& skill_dir,
                                int timeout_seconds) {
    std::string out;
    out.reserve(content.size());
    size_t pos = 0;
    while (true) {
        auto start = content.find("!`", pos);
        if (start == std::string::npos) {
            out.append(content, pos, std::string::npos);
            break;
        }
        auto end = content.find('`', start + 2);
        if (end == std::string::npos) {
            out.append(content, pos, std::string::npos);
            break;
        }
        out.append(content, pos, start - pos);
        std::string cmd = content.substr(start + 2, end - start - 2);
        auto timeout = std::chrono::seconds(std::max(1, timeout_seconds));
        auto result = process::exec(trim(cmd), skill_dir, timeout, 4096);
        if (result.ok()) {
            std::string output = result.value().stdout_output;
            while (!output.empty() && output.back() == '\n') output.pop_back();
            if (output.size() > 4000) output = output.substr(0, 4000) + "...[truncated]";
            out += output;
        } else {
            out += "[inline-shell error: " + result.error().message + "]";
        }
        pos = end + 1;
    }
    return out;
}

std::string preprocess(const std::string& content,
                       const SkillInfo& info,
                       const SkillOptions& opts) {
    std::string out = content;
    if (opts.template_vars) {
        out = replace_all(out, "${EA_SKILL_DIR}", info.directory);
        out = replace_all(out, "${EA_SKILL_NAME}", info.name);
    }
    if (opts.inline_shell) {
        out = expand_inline_shell(out, info.directory, opts.inline_shell_timeout);
    }
    return out;
}

}  // namespace

SkillManager::SkillManager(std::vector<std::string> roots,
                           std::vector<std::string> disabled,
                           SkillOptions options,
                           std::string user_dir)
    : roots_(std::move(roots))
    , disabled_(std::move(disabled))
    , options_(std::move(options))
    , user_dir_(std::move(user_dir)) {}

std::string SkillManager::build_cache_key() const {
    std::ostringstream oss;
    for (const auto& raw_root : roots_) {
        auto root = efs::expand_tilde(raw_root);
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        std::vector<fs::path> files;
        collect_skill_files(root, files);
        for (const auto& path : files) {
            auto mtime = fs::last_write_time(path, ec);
            auto size = fs::file_size(path, ec);
            oss << path.string() << ":" << mtime.time_since_epoch().count()
                << ":" << size << ";";
        }
    }
    return oss.str();
}

void SkillManager::invalidate_cache() const {
    cache_.clear();
    cache_key_.clear();
    cache_valid_ = false;
}

Result<std::vector<SkillInfo>> SkillManager::scan_impl(bool filter_disabled) const {
    std::vector<SkillInfo> result;
    std::set<std::string> seen_names;
    std::set<std::string> seen_dirs;

    for (const auto& raw_root : roots_) {
        auto root = efs::expand_tilde(raw_root);
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;

        std::vector<fs::path> files;
        collect_skill_files(root, files);
        for (const auto& skill_md : files) {
            auto rel_dir = fs::relative(skill_md.parent_path(), root, ec).generic_string();
            if (ec) continue;

            auto content = efs::read_file(skill_md.string());
            if (!content.ok()) continue;
            auto fm = parse_frontmatter(content.value());

            std::string skill_dir_name = skill_md.parent_path().filename().string();
            std::string skill_name = fm.name.empty() ? skill_dir_name : fm.name;
            std::string category = "general";
            auto slash = rel_dir.rfind('/');
            if (slash != std::string::npos) {
                category = rel_dir.substr(0, slash);
            }

            SkillInfo info;
            info.name = skill_name;
            info.description = fm.description;
            info.version = fm.version;
            info.category = category;
            info.directory = skill_md.parent_path().string();
            info.relative_dir = rel_dir;
            info.tags = fm.tags;
            info.platforms = fm.platforms;

            if (!platform_matches(fm.platforms)) continue;
            if (filter_disabled) {
                if (std::find(disabled_.begin(), disabled_.end(), skill_name) !=
                        disabled_.end() ||
                    std::find(disabled_.begin(), disabled_.end(), rel_dir) !=
                        disabled_.end()) {
                    continue;
                }
            }
            if (!seen_names.insert(skill_name).second ||
                !seen_dirs.insert(rel_dir).second) {
                continue;  // Earlier root wins
            }

            result.push_back(std::move(info));
        }
    }

    std::sort(result.begin(), result.end(),
              [](const SkillInfo& a, const SkillInfo& b) {
                  if (a.category != b.category) return a.category < b.category;
                  return a.name < b.name;
              });
    return result;
}

Result<std::vector<SkillInfo>> SkillManager::scan_cached() const {
    auto key = build_cache_key();
    if (cache_valid_ && key == cache_key_) {
        return cache_;
    }
    auto result = scan_impl();
    if (!result.ok()) return result;
    cache_ = result.value();
    cache_key_ = std::move(key);
    cache_valid_ = true;
    return cache_;
}

Result<std::vector<SkillInfo>> SkillManager::list() const {
    return scan_cached();
}

Result<SkillInfo> SkillManager::find_skill(const std::string& name) const {
    auto skills = scan_cached();
    if (!skills.ok()) return skills.error();
    for (const auto& info : skills.value()) {
        if (info.name == name || info.relative_dir == name ||
            info.directory.substr(info.directory.rfind('/') + 1) == name) {
            return info;
        }
    }
    return Error::not_found("Skill not found: " + name);
}

Result<std::string> SkillManager::view(const std::string& name,
                                       const std::string& file_path) const {
    if (!safe_relative(name)) {
        return Error::invalid_arg(
            "Skill name must be a relative path without '..' or absolute segments");
    }
    if (!file_path.empty() && !safe_relative(file_path)) {
        return Error::invalid_arg(
            "file_path must be a relative path without '..' or absolute segments");
    }

    auto found = find_skill(name);
    if (!found.ok()) return found.error();

    std::string path = join_path(found.value().directory, "SKILL.md");
    if (!file_path.empty()) {
        path = join_path(found.value().directory, file_path);
        return efs::read_file(path);
    }

    auto content = efs::read_file(path);
    if (!content.ok()) return content.error();
    return preprocess(content.value(), found.value(), options_);
}

std::string SkillManager::build_index() const {
    auto skills = scan_cached();
    if (!skills.ok() || skills.value().empty()) return "";

    std::map<std::string, std::vector<const SkillInfo*>> by_category;
    for (const auto& info : skills.value()) {
        by_category[info.category].push_back(&info);
    }

    std::ostringstream oss;
    oss << "# Skills (mandatory)\n"
        << "Before replying, scan the skills below. If a skill matches or is "
           "partially relevant to your task, you MUST load it with "
           "skill_view(name) and follow its instructions. Err on the side of "
           "loading — skills contain specialized knowledge and project conventions.\n"
        << "\n<available_skills>\n";
    for (const auto& [category, skills_in_cat] : by_category) {
        oss << "  " << category << ":\n";
        for (const auto* info : skills_in_cat) {
            oss << "    - " << info->name;
            if (!info->description.empty()) {
                oss << ": " << info->description;
            }
            oss << "\n";
        }
    }
    oss << "</available_skills>\n"
        << "\nOnly proceed without loading a skill if genuinely none are relevant.";
    return oss.str();
}

std::string SkillManager::primary_dir() const {
    if (!user_dir_.empty()) return efs::expand_tilde(user_dir_);
    if (!roots_.empty()) return efs::expand_tilde(roots_.front());
    return "";
}

Result<std::string> SkillManager::create(const std::string& name,
                                         const std::string& description,
                                         const std::string& content,
                                         const std::string& category) {
    if (!valid_skill_name(name)) {
        return Error::invalid_arg(
            "Skill name must be [a-zA-Z0-9_-]+ without path separators");
    }
    std::string cat = category.empty() ? "general" : category;
    if (!safe_relative(cat)) {
        return Error::invalid_arg("Invalid skill category");
    }

    auto base = primary_dir();
    if (base.empty()) return Error::io("No writable skills directory");
    auto dir = fs::path(base) / cat / name;
    std::error_code ec;
    if (fs::exists(dir / "SKILL.md", ec)) {
        return Error::invalid_arg("Skill already exists: " + name);
    }
    fs::create_directories(dir, ec);
    if (ec) return Error::io("Failed to create skill directory: " + ec.message());

    Frontmatter fm;
    fm.name = name;
    fm.description = description;
    fm.version = "1.0.0";
    fm.platforms = {"linux", "android"};
    std::string doc = serialize_frontmatter(fm) + "\n" + content;
    auto write = efs::write_file((dir / "SKILL.md").string(), doc);
    if (!write.ok()) return write.error();

    invalidate_cache();
    return name;
}

Result<void> SkillManager::update(const std::string& name,
                                  const std::string& content) {
    auto found = find_skill(name);
    if (!found.ok()) return found.error();

    std::string doc;
    if (content.rfind("---", 0) == 0) {
        doc = content;
    } else {
        auto existing = efs::read_file(join_path(found.value().directory, "SKILL.md"));
        if (!existing.ok()) return existing.error();
        auto fm = parse_frontmatter(existing.value());
        doc = serialize_frontmatter(fm) + "\n" + content;
    }

    auto write = efs::write_file(
        join_path(found.value().directory, "SKILL.md"), doc);
    if (!write.ok()) return write.error();
    invalidate_cache();
    return {};
}

Result<void> SkillManager::remove(const std::string& name) {
    auto found = find_skill(name);
    if (!found.ok()) return found.error();
    std::error_code ec;
    fs::remove_all(found.value().directory, ec);
    if (ec) return Error::io("Failed to remove skill: " + ec.message());
    invalidate_cache();
    return {};
}

Result<void> SkillManager::disable(const std::string& name) {
    auto found = find_skill(name);
    if (!found.ok()) return found.error();
    if (std::find(disabled_.begin(), disabled_.end(), found.value().name) ==
        disabled_.end()) {
        disabled_.push_back(found.value().name);
    }
    invalidate_cache();
    return {};
}

Result<void> SkillManager::enable(const std::string& name) {
    // Disabled skills are filtered out of the cached list, so search the
    // unfiltered scan to find them again.
    auto skills = scan_impl(false);
    if (!skills.ok()) return skills.error();
    const SkillInfo* found = nullptr;
    for (const auto& info : skills.value()) {
        if (info.name == name || info.relative_dir == name ||
            info.directory.substr(info.directory.rfind('/') + 1) == name) {
            found = &info;
            break;
        }
    }
    if (!found) return Error::not_found("Skill not found: " + name);
    disabled_.erase(
        std::remove(disabled_.begin(), disabled_.end(), found->name),
        disabled_.end());
    invalidate_cache();
    return {};
}

}  // namespace ea::skill
