# Skills

本项目参照 Hermes 的 skills 模型：每个技能是一个包含 `SKILL.md` 的目录，
按 `skills/<category>/<skill-name>/` 组织。

```text
skills/
├── development/
│   └── cpp-conventions/
│       └── SKILL.md
└── productivity/
    └── markdown-notes/
        └── SKILL.md
```

`SKILL.md` 开头用 YAML frontmatter 声明元数据：

```yaml
---
name: my-skill
description: "One-line description shown in the skills index."
version: 1.0.0
platforms: [linux, android]
tags: [example, workflow]
---

# My Skill

正文是给模型看的具体操作步骤、命令和注意事项。
```

支持字段：`name`、`description`、`version`、`platforms`、`tags`。
`platforms` 为空表示全平台可用；支持 `linux` / `android` / `wsl`。

系统提示词中只注入技能索引，模型需要时通过 `skill_view(name)` 加载正文。
技能目录还可以包含 `references/`、`templates/`、`assets/`、`scripts/`，
用 `skill_view(name, file_path="references/xxx.md")` 读取。

## 工具

- `skills_list` — 按分类/关键词列出技能
- `skill_view` — 加载技能正文或技能目录内文件
- `skill_manage` — `create` / `update` / `delete` / `disable` / `enable`

TUI 里可以直接使用 `/skills` 列出技能，`/skill <name>` 加载技能。

## 模板与预处理

- `${EA_SKILL_DIR}` 会替换为技能目录绝对路径
- `${EA_SKILL_NAME}` 会替换为技能名
- `!`cmd`` 内联 shell 片段默认不执行，开启 `[skills] inline_shell = true` 后
  会在技能目录下执行并替换为 stdout（上限 4000 字节，默认超时 10 秒）

技能索引按文件 mtime/size 缓存，外部修改 SKILL.md 后会自动重新扫描。

发现目录：

- `~/.embedded-agent/skills`
- 当前目录 `./skills`
- `[skills].dirs` 中配置的额外目录
- `EA_SKILLS_DIR` 环境变量指定的额外目录
