# Lua Prozess-Start API und Plattform-Metadaten im Runtime-Modul

## Summary

Add a first-class Lua process module that can start native processes with executable path, args, and working directory, while refactoring MCP stdio launching to reuse the same native process infrastructure. Also expose `yaaf.platform` so Lua scripts can branch on `windows`, `linux`, or `osx`.

## Problem

Today, process spawning logic is embedded in MCP stdio transport internals, so Lua scripts cannot directly start child processes through a supported runtime API. In addition, Lua scripts currently have no stable `yaaf.platform` field for explicit OS-specific behavior.

## Goal

Provide an explicit, testable Lua API for process startup and I/O, backed by a shared native process layer reused by MCP stdio, and expose stable platform metadata via `yaaf.platform`.

## Scope

- Add shared native process launch abstraction in libyaaf with:
  - executable path
  - args array
  - working directory
  - optional env overrides
  - stdin/stdout pipe wiring
- Refactor MCP stdio process startup to use the shared abstraction.
- Add Lua module `process` with `process.start(...)` and a returned handle for stdio interaction and lifecycle.
- Add `yaaf.platform` with values: `windows`, `linux`, `osx`.
- Add/adjust focused tests and docs.

## Non-Goals

- No shell parsing/execution (`cmd /c`, `/bin/sh -c`) as default behavior.
- No PTY/terminal emulation support in this change.
- No full process tree management or advanced signal orchestration.
- No breaking rewrite of MCP config shape.

## Assumptions, Dependencies, And Risks

- Assumption: API will be handle-based (`process.start`) to match MCP stdio streaming needs and existing native launch patterns.
- Assumption: `yaaf.platform` is a normalized string with exactly `windows`, `linux`, `osx`.
- Dependency: platform-specific launch internals must compile cleanly on WIN32 and UNIX targets.
- Risk: POSIX working-directory handling may require moving away from pure `posix_spawnp` in some paths; tests must lock behavior.
- Risk: Windows command-line quoting and cwd handling can regress MCP stdio if not covered by integration tests.

## Acceptance Criteria

- [ ] Lua can start a process with executable path, args, and working directory via `process.start`.
- [ ] Returned Lua process handle supports minimum lifecycle and stdio operations required by scripted MCP-like flows (write/read/wait/close/terminate as designed).
- [ ] MCP stdio client path uses the shared process launcher and existing MCP stdio integration behavior remains green.
- [ ] `yaaf.platform` is available and equals one of `windows`, `linux`, `osx`.
- [ ] Script-level tests cover `process.start` success path and at least one failure path (missing executable or invalid cwd).
- [ ] Docs are updated for new module/API and `yaaf.platform`.

## Task Legend

- `[ ]` not started
- `[-]` in progress
- `[x]` completed
- `[!]` blocked or waiting
- `[?]` user decision required

## Tracker

| Phase | Status | Notes |
| --- | --- | --- |
| Discovery | [x] | API contracts established; research in .github/research/lua-process-api-contracts.md |
| Implementation | [ ] | Shared native process layer + MCP refactor + Lua module |
| Validation | [ ] | Build and focused tests for Lua + MCP stdio |
| Documentation | [ ] | Module docs + yaaf runtime docs updated |

## Phase 1 - Discovery

- [x] Define shared native process contract and migration seam
  - [x] Document current MCP stdio launch responsibilities in platform files and list what moves to shared code
  - [x] Freeze Lua API shape for `process.start(descriptor)` and returned handle methods
  - [x] Define error mapping strategy from native exceptions to Lua errors/results

- [x] Define platform metadata source
  - [x] Add/confirm native platform-name helper in platform layer
  - [x] Confirm mapping rules to `windows` / `linux` / `osx`

## Phase 2 - Implementation

- [ ] Add shared process launch abstraction in native layer
  - [ ] Introduce platform-agnostic process options/result types (command, args, cwd, env, pipes)
  - [ ] Implement WIN32 backend using CreateProcess with current-directory support
  - [ ] Implement POSIX backend with cwd + argv + env + stdin/stdout pipe wiring
  - [ ] Wire new source files into libyaaf CMake target

- [ ] Refactor MCP stdio to use shared launcher
  - [ ] Replace duplicated launch/path/env/pipe setup in MCP stdio platform files with shared abstraction
  - [ ] Preserve existing read/write timeout and process-exit semantics
  - [ ] Keep public MCP behavior unchanged

- [ ] Add Lua `process` module
  - [ ] Create module registration and runtime context plumbing in Lua runtime bootstrap
  - [ ] Implement `process.start({ command, args?, cwd?, env? })`
  - [ ] Return process handle object with agreed operations for write/read/wait/close/terminate
  - [ ] Ensure Lua-facing validation errors are actionable

- [ ] Add `yaaf.platform`
  - [ ] Extend yaaf context with platform string
  - [ ] Export field in `require("yaaf")` module table
  - [ ] Ensure value is normalized and stable across scripts

## Phase 3 - Validation

- [ ] Add/extend tests for Lua process module
  - [ ] Add script-driven tests in existing Lua mock test suite for happy-path start/read/write/wait
  - [ ] Add negative test for invalid command or invalid working directory
  - [ ] Add assertion that `yaaf.platform` exists and is one of expected values

- [ ] Add/extend MCP stdio regression tests
  - [ ] Verify env + args behavior remains intact
  - [ ] Add coverage for working-directory-sensitive child process startup in MCP stdio path (where feasible with existing scripted fixtures)

- [ ] Build and run focused targets
  - [ ] Build relevant test targets
  - [ ] Run targeted integration + mock test cases
  - [ ] Triage and fix regressions from process refactor

## Phase 4 - Documentation

- [ ] Document process module API
  - [ ] Add docs page for `process` module (descriptor fields, handle methods, errors)
  - [ ] Add one minimal runnable Lua example for process start with cwd + args

- [ ] Update yaaf docs for platform field
  - [ ] Update yaaf module reference to include `yaaf.platform`
  - [ ] Update runtime overview page to mention process module and platform branching
  - [ ] Cross-link process docs from module index
