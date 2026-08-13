#pragma once
#include "skill/Skill.h"
#include "tool/ITool.h"

namespace ea::tool {

class SkillsListTool : public ITool {
public:
    explicit SkillsListTool(skill::SkillManager* skills) : skills_(skills) {}

    std::string name() const override { return "skills_list"; }
    std::string description() const override {
        return "List available skills grouped by category";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return false; }

private:
    skill::SkillManager* skills_;
};

class SkillViewTool : public ITool {
public:
    explicit SkillViewTool(skill::SkillManager* skills) : skills_(skills) {}

    std::string name() const override { return "skill_view"; }
    std::string description() const override {
        return "Load the full content of a skill and follow its instructions";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return false; }

private:
    skill::SkillManager* skills_;
};

class SkillManageTool : public ITool {
public:
    explicit SkillManageTool(skill::SkillManager* skills) : skills_(skills) {}

    std::string name() const override { return "skill_manage"; }
    std::string description() const override {
        return "Create, update, delete, enable, or disable skills";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    skill::SkillManager* skills_;
};

class PluginInstallTool : public ITool {
public:
    explicit PluginInstallTool(skill::PluginManager* plugins) : plugins_(plugins) {}

    std::string name() const override { return "plugin_install"; }
    std::string description() const override {
        return "Install a plugin from a git URL or marketplace name; its skills "
               "become available after restart";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    skill::PluginManager* plugins_;
};

class PluginMarketplaceTool : public ITool {
public:
    explicit PluginMarketplaceTool(skill::PluginManager* plugins) : plugins_(plugins) {}

    std::string name() const override { return "plugin_marketplace"; }
    std::string description() const override {
        return "Add, list, or remove plugin marketplaces";
    }
    json parameters_schema() const override;
    Result<ToolResult> execute(const json& args) override;
    bool is_mutating() const override { return true; }
    bool is_dangerous() const override { return true; }

private:
    skill::PluginManager* plugins_;
};

}  // namespace ea::tool
