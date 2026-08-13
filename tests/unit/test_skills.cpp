#include <catch2/catch_test_macros.hpp>
#include "skill/Skill.h"
#include "tool/SkillTools.h"
#include "io/FileSystem.h"
#include <algorithm>
#include <filesystem>

using namespace ea;
using namespace ea::skill;
namespace fsys = std::filesystem;

namespace {

std::string make_skill_dir() {
    auto base = fsys::temp_directory_path() / "ea_skill_test";
    fsys::remove_all(base);
    fsys::create_directories(base / "research/arxiv/references");
    fsys::create_directories(base / "development");
    fsys::create_directories(base / "productivity");
    fsys::create_directories(base / "development/cpp-conventions");
    fsys::create_directories(base / "productivity/secret-skill");
    fsys::create_directories(base / "development/windows-only");

    ea::fs::write_file(
        (base / "research/arxiv/SKILL.md").string(),
        R"(---
name: arxiv
description: "Search arXiv papers by keyword or ID."
version: 1.0.0
platforms: [linux, android]
tags: [research, papers]
---

# arXiv

Search academic papers on arXiv.
)");

    ea::fs::write_file(
        (base / "research/arxiv/references/api.md").string(),
        "API ref\n");
    ea::fs::write_file(
        (base / "research/arxiv/references/old/SKILL.md").string(),
        "---\nname: stale\n---\n# Stale\n");

    ea::fs::write_file(
        (base / "development/cpp-conventions/SKILL.md").string(),
        R"(---
name: cpp-conventions
description: "C++ conventions and review checklist."
version: 2.0.0
platforms: [linux]
tags: [cpp, review]
---

# C++ Conventions

Follow the project conventions.
)");

    ea::fs::write_file(
        (base / "productivity/secret-skill/SKILL.md").string(),
        R"(---
name: secret-skill
description: "Disabled in tests."
---

# Secret
)");

    ea::fs::write_file(
        (base / "development/windows-only/SKILL.md").string(),
        R"(---
name: windows-only
description: "Should be filtered out on Linux."
platforms: [windows]
---

# Windows Only
)");

    return base.string();
}

}  // namespace

TEST_CASE("SkillManager discovers and parses SKILL.md files", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base}, {"secret-skill"});

    auto result = manager.list();
    REQUIRE(result.ok());
    const auto& skills = result.value();
    REQUIRE(skills.size() == 2);

    auto arxiv = std::find_if(skills.begin(), skills.end(),
        [](const SkillInfo& s) { return s.name == "arxiv"; });
    REQUIRE(arxiv != skills.end());
    REQUIRE(arxiv->category == "research");
    REQUIRE(arxiv->relative_dir == "research/arxiv");
    REQUIRE(arxiv->description.find("arXiv") != std::string::npos);
    REQUIRE(arxiv->tags.size() == 2);

    auto cpp = std::find_if(skills.begin(), skills.end(),
        [](const SkillInfo& s) { return s.name == "cpp-conventions"; });
    REQUIRE(cpp != skills.end());
    REQUIRE(cpp->category == "development");
    REQUIRE(cpp->version == "2.0.0");

    fsys::remove_all(base);
}

TEST_CASE("SkillManager view loads skills and reference files", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base});

    auto by_name = manager.view("arxiv");
    REQUIRE(by_name.ok());
    REQUIRE(by_name.value().find("# arXiv") != std::string::npos);

    auto by_path = manager.view("research/arxiv");
    REQUIRE(by_path.ok());
    REQUIRE(by_path.value() == by_name.value());

    auto ref = manager.view("arxiv", "references/api.md");
    REQUIRE(ref.ok());
    REQUIRE(ref.value() == "API ref\n");

    REQUIRE_FALSE(manager.view("../etc/passwd").ok());
    REQUIRE_FALSE(manager.view("/etc/passwd").ok());
    REQUIRE_FALSE(manager.view("arxiv", "../SKILL.md").ok());
    REQUIRE_FALSE(manager.view("no-such-skill").ok());

    fsys::remove_all(base);
}

TEST_CASE("SkillManager build_index renders available skills", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base}, {"secret-skill"});

    auto index = manager.build_index();
    REQUIRE(index.find("<available_skills>") != std::string::npos);
    REQUIRE(index.find("research:") != std::string::npos);
    REQUIRE(index.find("arxiv") != std::string::npos);
    REQUIRE(index.find("cpp-conventions") != std::string::npos);
    REQUIRE(index.find("secret-skill") == std::string::npos);
    REQUIRE(index.find("windows-only") == std::string::npos);

    fsys::remove_all(base);
}

TEST_CASE("SkillsListTool filters by category and query", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base});
    tool::SkillsListTool tool(&manager);

    auto all = tool.execute(json::object());
    REQUIRE(all.ok());
    REQUIRE(all.value().output.find("arxiv") != std::string::npos);
    REQUIRE(all.value().output.find("cpp-conventions") != std::string::npos);

    auto by_category = tool.execute(json::parse(R"({"category":"research"})"));
    REQUIRE(by_category.ok());
    REQUIRE(by_category.value().output.find("arxiv") != std::string::npos);
    REQUIRE(by_category.value().output.find("cpp-conventions") == std::string::npos);

    auto by_query = tool.execute(json::parse(R"({"query":"papers"})"));
    REQUIRE(by_query.ok());
    REQUIRE(by_query.value().output.find("arxiv") != std::string::npos);

    auto none = tool.execute(json::parse(R"({"query":"zzz"})"));
    REQUIRE(none.ok());
    REQUIRE(none.value().output == "No skills found.");

    fsys::remove_all(base);
}

TEST_CASE("SkillViewTool returns content or a tool error", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base});
    tool::SkillViewTool tool(&manager);

    auto ok_result = tool.execute(json::parse(R"({"name":"arxiv"})"));
    REQUIRE(ok_result.ok());
    REQUIRE_FALSE(ok_result.value().is_error);
    REQUIRE(ok_result.value().output.find("Search academic papers") != std::string::npos);

    auto missing = tool.execute(json::parse(R"({"name":"nope"})"));
    REQUIRE(missing.ok());
    REQUIRE(missing.value().is_error);

    auto bad_args = tool.execute(json::parse(R"({})"));
    REQUIRE_FALSE(bad_args.ok());

    fsys::remove_all(base);
}

TEST_CASE("SkillManageTool creates, updates, disables, enables, and deletes", "[skill]") {
    auto base = make_skill_dir();
    SkillManager manager({base});
    tool::SkillManageTool tool(&manager);

    auto created = tool.execute(json::parse(
        R"({"action":"create","name":"managed","description":"Managed skill","content":"# Managed\nBody","category":"general"})"));
    REQUIRE(created.ok());
    REQUIRE_FALSE(created.value().is_error);

    auto list = manager.list();
    REQUIRE(list.ok());
    REQUIRE(std::any_of(list.value().begin(), list.value().end(),
        [](const SkillInfo& s) { return s.name == "managed"; }));

    auto updated = tool.execute(json::parse(
        R"({"action":"update","name":"managed","content":"# Managed\nNew body"})"));
    REQUIRE(updated.ok());
    REQUIRE_FALSE(updated.value().is_error);
    auto viewed = manager.view("managed");
    REQUIRE(viewed.ok());
    REQUIRE(viewed.value().find("New body") != std::string::npos);

    auto disabled = tool.execute(json::parse(
        R"({"action":"disable","name":"managed"})"));
    REQUIRE(disabled.ok());
    REQUIRE_FALSE(disabled.value().is_error);
    list = manager.list();
    REQUIRE(list.ok());
    REQUIRE(std::none_of(list.value().begin(), list.value().end(),
        [](const SkillInfo& s) { return s.name == "managed"; }));

    auto enabled = tool.execute(json::parse(
        R"({"action":"enable","name":"managed"})"));
    REQUIRE(enabled.ok());
    REQUIRE_FALSE(enabled.value().is_error);
    list = manager.list();
    REQUIRE(list.ok());
    REQUIRE(std::any_of(list.value().begin(), list.value().end(),
        [](const SkillInfo& s) { return s.name == "managed"; }));

    auto deleted = tool.execute(json::parse(
        R"({"action":"delete","name":"managed"})"));
    REQUIRE(deleted.ok());
    REQUIRE_FALSE(deleted.value().is_error);
    list = manager.list();
    REQUIRE(list.ok());
    REQUIRE(std::none_of(list.value().begin(), list.value().end(),
        [](const SkillInfo& s) { return s.name == "managed"; }));

    fsys::remove_all(base);
}

TEST_CASE("SkillManager caches by mtime and preprocesses templates/inline shell", "[skill]") {
    auto base = make_skill_dir();
    SkillOptions opts;
    opts.inline_shell = true;
    SkillManager manager({base}, {}, opts, base + "/user");

    auto created = manager.create("template-skill", "Template skill",
        "${EA_SKILL_DIR}\n!`echo hi`", "general");
    REQUIRE(created.ok());

    auto viewed = manager.view("template-skill");
    REQUIRE(viewed.ok());
    REQUIRE(viewed.value().find(base + "/user/general/template-skill") != std::string::npos);
    REQUIRE(viewed.value().find("hi") != std::string::npos);

    // Cache: first read, then external edit, then read again.
    auto before = manager.list();
    REQUIRE(before.ok());
    ea::fs::write_file(
        (fsys::path(base) / "user/general/template-skill/SKILL.md").string(),
        "---\nname: template-skill\ndescription: \"Edited externally.\"\n---\n\n# New\n");
    auto after = manager.list();
    REQUIRE(after.ok());
    auto it = std::find_if(after.value().begin(), after.value().end(),
        [](const SkillInfo& s) { return s.name == "template-skill"; });
    REQUIRE(it != after.value().end());
    REQUIRE(it->description == "Edited externally.");

    fsys::remove_all(base);
}

TEST_CASE("PluginManager validates URLs, lists and removes plugins", "[skill]") {
    auto base = fsys::temp_directory_path() / "ea_plugin_test";
    fsys::remove_all(base);
    PluginManager pm((base / "plugins").string(),
                     (base / "marketplace.json").string());

    auto unsafe = pm.install("https://example.com/repo;rm -rf /");
    REQUIRE_FALSE(unsafe.ok());

    auto unknown = pm.install("does-not-exist");
    REQUIRE_FALSE(unknown.ok());
    REQUIRE(ea::fs::exists((base / "marketplace.json").string()).value());

    auto roots = pm.skill_roots();
    REQUIRE(roots.ok());
    REQUIRE(roots.value().empty());

    auto skill_dir = base / "plugins" / "demo" / "skills" / "general" / "x";
    fsys::create_directories(skill_dir);
    ea::fs::write_file((skill_dir / "SKILL.md").string(),
                       "---\nname: x\n---\n# X\n");

    roots = pm.skill_roots();
    REQUIRE(roots.ok());
    REQUIRE(roots.value().size() == 1);
    REQUIRE(roots.value()[0].find("demo/skills") != std::string::npos);

    auto list = pm.list();
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 1);
    REQUIRE(list.value()[0].name == "demo");

    REQUIRE(pm.remove("demo").ok());
    list = pm.list();
    REQUIRE(list.ok());
    REQUIRE(list.value().empty());

    auto mkt_dir = base / "marketplaces" / "demo" / ".claude-plugin";
    fsys::create_directories(mkt_dir);
    ea::fs::write_file(
        (mkt_dir / "marketplace.json").string(),
        R"json({
          "name": "demo",
          "plugins": [
            {"name": "alpha", "source": {"source": "url", "url": "https://example.com/alpha.git"}}
          ]
        })json");

    auto marketplaces = pm.list_marketplaces();
    REQUIRE(marketplaces.ok());
    REQUIRE(marketplaces.value().size() == 1);
    REQUIRE(marketplaces.value()[0].name == "demo");

    REQUIRE_FALSE(pm.install("missing@demo").ok());
    REQUIRE_FALSE(pm.install("alpha@not-registered").ok());
    REQUIRE(pm.remove_marketplace("demo").ok());

    fsys::remove_all(base);
}
