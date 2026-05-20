---
name: yaaf-covdbg-unit-test-coverage
description: 'Run native unit test coverage with covdbg in this C++20/CMake project. Use when evaluating gtest coverage, finding uncovered C++ code, deciding whether to add tests or remove dead code, and iterating on coverage for libyaaf without using CTest.'
argument-hint: 'What coverage goal or target should be evaluated?'
user-invocable: true
disable-model-invocation: false
---

# covdbg Unit Test Coverage

Use this skill to run the repository's native unit tests under `covdbg`, inspect uncovered code, improve coverage with focused tests, and summarize what remains uncovered.

This repository is project-specific:

- Language/build stack: C++20 + CMake
- Test style: plain gtest binaries, not CTest
- Primary product code target: `libyaaf`
- Current test binary pattern is discovered from the build output, typically `build/**/http_client_tests.exe`
- Coverage config lives in `.covdbg.yaml`
- Coverage excludes `tests/**` and `app/main.cpp`, so the main focus is product code under `libyaaf/**`

## When to Use

Use this skill when you need to:

- run `covdbg` coverage on the project's gtest binaries
- inspect which product source files are still uncovered
- determine whether uncovered code needs more tests, a refactor for testability, or dead-code removal
- validate that a test change actually improves measured coverage
- produce a concise coverage summary with next actions

## Decision Rules

Classify uncovered code before editing:

1. **Reachable public behavior** → add or refine tests
2. **Hidden external failure path** (for example direct libcurl failure branches) → prefer introducing a seam or wrapper before trying to test it
3. **Truly obsolete or unused code** → remove it
4. **Defensive runtime-failure branch with no seam** → document it as intentionally remaining uncovered

Good candidates for additional tests:

- move/copy operations
- public API variants
- option/default branches
- parsing and serialization branches

Poor candidates without refactoring:

- global initialization failures
- allocation/resource failure branches in third-party APIs
- direct external library error branches that cannot be forced deterministically

## Step-by-Step Procedure

### 1. Discover the covdbg workspace state

Start with `covdbg_explore`.

Goal:

- confirm the resolved `.covdbg.yaml`
- identify the active `.covdb`
- discover the real test executable paths
- avoid inventing binary names or paths

Use the discovered executable paths exactly as returned.

### 2. Ensure the test binary is current

If source or tests changed, rebuild the affected gtest executable before coverage.

In this repository, the main focused test target is usually:

- `http_client_tests`

Do not rely on stale binaries when evaluating coverage changes.

### 3. Run coverage on real test executables

Use `covdbg_run` with one or more discovered executable paths.

Expected outcome:

- tests execute under coverage instrumentation
- the active workspace coverage database is refreshed
- you get a file and line summary immediately

### 4. Identify the next uncovered file

Use `covdbg_files` after a successful coverage run.

Choose the highest-value uncovered product file, usually based on:

- low coverage percent
- small number of remaining uncovered lines
- relation to the user's current task

### 5. Inspect uncovered segments

Use `covdbg_code` for the selected source file.

For each uncovered segment, answer:

- Is this reachable from the public API?
- Can it be covered with a black-box test?
- Does it require dependency injection or a seam?
- Is it obsolete or effectively dead?

### 6. Choose an action

#### If the path is reachable

Add or refine unit tests.

Examples:

- add a test for a second API variant
- exercise move construction / move assignment
- cover default parameter behavior
- validate a previously untested branch of response parsing

#### If the path is blocked by direct external dependencies

Refactor for testability instead of forcing brittle tests.

Examples:

- introduce a wrapper around global initialization
- isolate handle creation behind a factory
- wrap third-party list or resource construction behind an interface

#### If the code is dead

Remove it and rerun coverage.

Only do this when the code is clearly obsolete and not part of an intended defensive contract.

### 7. Validate the change loop

After edits:

1. rebuild the affected test executable
2. rerun `covdbg_run`
3. rerun `covdbg_files`
4. rerun `covdbg_code` for the touched source file

Do not assume coverage improved until the refreshed `.covdb` confirms it.

### 8. Stop when the remaining gaps are justified

It is acceptable to stop when the remaining uncovered lines are limited to defensive or non-deterministic third-party failure paths and there is no current seam to test them cleanly.

## Quality Checks

A coverage pass is complete when all of the following are true:

- the relevant gtest binary was rebuilt after code or test changes
- `covdbg_run` completed successfully and loaded coverage
- `covdbg_files` was reviewed after the latest run
- each remaining uncovered segment was classified as one of:
  - needs more tests
  - needs refactor for testability
  - dead code
  - intentionally uncovered defensive path
- the final report includes:
  - coverage percentage before and after changes
  - files touched
  - tests added or code removed
  - what still remains uncovered and why

## Repo-Specific Notes

- This repo intentionally uses plain gtest executables rather than CTest
- `.covdbg.yaml` excludes `tests/**` and `app/main.cpp`, so uncovered product code should normally be sought in `libyaaf/**`
- `http_client.cpp` may retain uncovered libcurl failure branches unless a seam is introduced around direct curl calls

## Output Template

When finishing, summarize in this structure:

1. **Coverage run**
   - which executable(s) were run
   - overall coverage result
2. **Findings**
   - uncovered files
   - notable uncovered segments
3. **Changes made**
   - tests added
   - code removed or refactored
4. **Coverage delta**
   - before → after
5. **Remaining gaps**
   - justified reasons for anything still uncovered
6. **Next best step**
   - test addition, seam extraction, or stop

## Example Prompts

- `/yaaf-covdbg-unit-test-coverage Evaluate current unit test coverage for libyaaf and improve the easiest remaining gaps.`
- `/yaaf-covdbg-unit-test-coverage Run coverage for http_client_tests, inspect uncovered code, and add focused tests where appropriate.`
- `/yaaf-covdbg-unit-test-coverage Check whether the remaining uncovered lines in http_client.cpp are dead code or defensive libcurl failure paths.`
