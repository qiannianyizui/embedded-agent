---
name: cpp-conventions
description: "C++ coding conventions and code-review checklist for this project."
version: 1.0.0
platforms: [linux, android]
tags: [cpp, code-review, conventions]
---

# C++ Conventions

Follow these conventions when writing or reviewing C++ code in this repository.

## Style

- Use C++17 and the project's `ea` namespace.
- Prefer `ea::Result<T>` over exceptions for error handling.
- Keep public headers self-contained with `#pragma once`.
- Use `snake_case` for functions and variables, `PascalCase` for types.
- Include what you use; avoid transitive include chains.

## Review Checklist

- No raw `new`/`delete` where a smart pointer or RAII type works.
- No unbounded recursion or blocking sleeps in hot paths.
- New features include unit tests under `tests/unit/`.
- Avoid `using namespace std;` in headers.

## Workflow

Before finishing a change, run the build and the relevant test binary:

```bash
cmake --build build -j
build/tests/unit/ea-unit-tests
```
