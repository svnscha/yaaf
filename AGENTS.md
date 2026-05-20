# Repository Agent Flow

This repository is a C++20 and CMake codebase centered on `libyaaf`.

## Default Working Loop

1. Start from the user prompt and identify the concrete behavior, failing path, or target file before editing.
2. Implement the smallest correct change at the root cause instead of layering on a workaround.
3. Add or update focused tests for the affected behavior.
4. Build the affected target and fix any compile or test failures introduced by the change.
5. Run coverage with `covdbg` against the real test executable, inspect uncovered product code, and improve tests when the remaining gaps are still reachable.
6. Repeat the implement, test, build, and coverage loop until the behavior is correct and the touched code is reasonably tested.

## Validation Expectations

- Prefer narrow validation first: targeted tests, then the smallest relevant build.
- Keep `tests/integration/` for real endpoint and real model/client tests only; tests that inject fake services, callbacks, or mocked model responses belong under `tests/mock/`.
- Treat coverage as a decision tool, not a vanity metric: add tests for reachable behavior, refactor when code is only blocked by poor seams, and leave clearly defensive third-party failure paths justified but uncovered.
- Keep edits focused and consistent with repository conventions, including the shared precompiled-header setup under `libyaaf/pch/`.

## Lua Runtime Design

- Document and design Lua APIs by user-facing module name, not by implementation location. Users should not need to care whether a module is native C++ or shipped Lua.
- When a module is built into the runtime, mention that in the first sentence of that module's documentation, then describe the API normally.
- Keep native runtime registries in singular modules such as `agent` and `tool`; keep concrete Lua implementations under directories such as `lua/agents/` and `lua/tools/`.
- Avoid compatibility aliases for old Lua module names while the project is in early development. Prefer clean module names and update docs, tests, and examples together.
- Keep module documentation split under `docs/modules/`, one page per public module, so the reference stays scannable.
- Keep tool authoring and built-in tool documentation in `docs/tools/`; link from Lua API pages instead of duplicating registry guidance.
