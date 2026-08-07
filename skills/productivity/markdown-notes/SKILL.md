---
name: markdown-notes
description: "Create and maintain structured Markdown notes and knowledge bases."
version: 1.0.0
platforms: [linux, android]
tags: [markdown, notes, knowledge-base]
---

# Markdown Notes

Use this skill when the user asks to create, organize, or maintain notes and
knowledge bases in Markdown.

## Structure

Prefer a simple folder layout:

```text
notes/
├── index.md
├── projects/
└── references/
```

## Conventions

- Keep one topic per file.
- Start each file with an H1 title matching the filename.
- Use `index.md` as a link catalog with one-line summaries.
- Prefer relative links between notes.
- Store raw source material under `references/` and keep synthesis in the
  main note.

## Maintenance

When asked to extend an existing knowledge base, first read `index.md` and
update it whenever a note is added or renamed.
