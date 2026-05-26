# Vendor vcpkg as a cross-platform submodule

## Summary

Make vcpkg a repository submodule on every supported platform so contributor setup, local builds, devcontainer use, and CI all rely on the same checked-in vcpkg location and bootstrap flow.

## Problem

The repository currently assumes multiple vcpkg acquisition paths. `CMakeLists.txt` can use `VCPKG_ROOT`, a repo-local `./vcpkg`, or a Visual Studio-installed copy. Contributor docs tell macOS and Linux users to clone vcpkg into `$HOME/vcpkg` and export `VCPKG_ROOT`. CI checks out vcpkg separately in workflows, and the devcontainer rebuilds `./vcpkg` by fetching a pinned commit in `.devcontainer/setup-vcpkg.sh`. That split makes setup inconsistent across platforms, duplicates the pin in multiple places, and creates drift between local development, devcontainer, and CI.

## Goal

Use a single repo-owned vcpkg submodule and one documented bootstrap/configure story across Windows, macOS, Linux, devcontainer, and CI, while preserving the current release and packaging behavior.

## Scope

- Add vcpkg as a tracked git submodule at `./vcpkg` pinned to the current baseline commit.
- Refactor build configuration so the repository-local submodule is the default and documented path on all supported platforms.
- Update CI and devcontainer setup to initialize and bootstrap the submodule instead of checking out or reconstructing vcpkg separately.
- Update contributor docs and workflow notes to describe the new clone, submodule, bootstrap, and update flow.

## Non-Goals

- Replacing vcpkg with another package manager.
- Changing the current dependency set in `vcpkg.json` beyond what is needed for the submodule transition.
- Reworking the Linux musl release design, packaging layout, or runtime smoke coverage outside of submodule-related adjustments.
- Adding platform-specific fallback package discovery paths beyond what is needed for a smooth migration.

## Assumptions, Dependencies, And Risks

- The submodule should be pinned to the same vcpkg commit already used by `vcpkg.json` builtin-baseline and by the current devcontainer and CI setup.
- The repository should prefer the vendored submodule over user-global `VCPKG_ROOT` or Visual Studio-integrated copies to keep setup consistent.
- CI jobs and contributors will use `git submodule update --init --recursive` as part of clone/setup.
- Dependabot does not currently manage vcpkg version bumps here, so the initial transition can keep submodule updates manual and document the bump process.
- The `.gitignore` entry for `vcpkg/` must be removed so the submodule is tracked.

## Acceptance Criteria

- [ ] A fresh clone can initialize vcpkg with repository-local commands on Windows, macOS, and Linux without requiring `$HOME/vcpkg` or `VCPKG_ROOT`.
- [ ] Default contributor build docs on macOS and Linux use the repo submodule path and a shared setup sequence.
- [ ] CI no longer checks out vcpkg as a separate repository or reconstructs it through a detached-fetch script.
- [ ] Devcontainer setup uses the checked-in submodule and still configures the musl toolchain successfully.
- [ ] CMake configuration continues to resolve the repo-local `vcpkg/scripts/buildsystems/vcpkg.cmake` for supported build paths.
- [ ] The submodule pin and documented update flow are clear enough to keep local, container, and CI environments aligned.

## Task Legend

- `[ ]` not started
- `[-]` in progress
- `[x]` completed
- `[!]` blocked or waiting
- `[?]` user decision required

## Tracker

| Phase | Status | Notes |
| --- | --- | --- |
| Discovery | [x] | Baseline pinned at `e5a4f54c0d562059e9ccc6f7e7150667da58fe41`; current references inventoried across CMake, docs, devcontainer, and workflows. |
| Implementation | [x] | Added the vendored `vcpkg` submodule, made CMake submodule-first, and updated devcontainer and workflow setup to initialize submodules instead of checking out vcpkg separately. |
| Validation | [x] | Local macOS and fresh-clone validation passed, and PR #26 CI confirmed Windows, macOS, Linux musl, and runtime smoke coverage. |
| Documentation | [x] | README and usage docs now describe clone-with-submodules, vendored bootstrap, Windows/macOS/Linux setup, musl reproduction, and baseline bump maintenance. |

## Phase 1 - Discovery

- [x] Inventory the current vcpkg acquisition and pinning surfaces.
  - [x] Confirm the pinned vcpkg commit used by `vcpkg.json`, `.devcontainer/setup-vcpkg.sh`, and workflow checkouts.
  - [x] Confirm every build and setup entry point that still references `VCPKG_ROOT`, external checkouts, or Visual Studio fallback behavior.
- [x] Decide the steady-state submodule workflow.
  - [x] Define the default clone and bootstrap commands contributors should use on every platform.
  - [x] Define how maintainers will bump the vcpkg submodule and keep `builtin-baseline` aligned.

### Discovery Notes

- Shared pinned vcpkg commit confirmed: `e5a4f54c0d562059e9ccc6f7e7150667da58fe41` in `vcpkg.json`, `.devcontainer/setup-vcpkg.sh`, `ci.yml`, and `dependabot-auto-merge.yml`.
- Current acquisition paths confirmed: repo-local `./vcpkg`, `VCPKG_ROOT`, Windows Visual Studio fallback in `CMakeLists.txt`, workflow-side secondary checkouts, and devcontainer detached-fetch setup.
- Steady-state workflow for this task: contributors clone with submodules or run `git submodule update --init --recursive`, bootstrap the repo-local `./vcpkg`, and keep the submodule commit plus `builtin-baseline` aligned during dependency bumps.

## Phase 2 - Repository And Build Refactor

- [x] Track vcpkg as a repository submodule.
  - [x] Add `.gitmodules` with `vcpkg` pointing at `https://github.com/microsoft/vcpkg.git` and pin it to the intended commit.
  - [x] Remove the `vcpkg/` ignore rule so the submodule is tracked correctly.
- [x] Make the repo-local submodule the primary build path.
  - [x] Simplify `CMakeLists.txt` toolchain resolution to prefer `${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake` as the default repository path.
  - [x] Re-evaluate whether Windows-specific Visual Studio vcpkg fallback should remain; remove it if it conflicts with the goal of one consistent workflow.
  - [x] Keep `CMakePresets.json` and any direct configure commands aligned with the vendored submodule path.
- [x] Refactor devcontainer setup around the submodule.
  - [x] Replace `.devcontainer/setup-vcpkg.sh` detached-fetch logic with submodule initialization or validation plus bootstrap.
  - [x] Keep musl-specific env vars and overlay triplet wiring unchanged except where the setup mechanism changes.

## Phase 3 - CI And Automation

- [x] Update GitHub Actions workflows to use the vendored submodule.
  - [x] Change workflow checkout steps to fetch submodules instead of performing a second `actions/checkout` for `microsoft/vcpkg`.
  - [x] Remove duplicated bootstrap/setup logic that exists only to materialize a standalone `vcpkg` checkout.
  - [x] Keep Windows, macOS, Linux musl, packaging, and runtime smoke jobs behaviorally equivalent after the setup change.
- [x] Update dependency-maintenance expectations.
  - [x] Remove now-obsolete workflow assumptions about separately checked-out vcpkg.
  - [x] Document or script the maintainer flow for advancing the submodule and syncing `vcpkg.json` baseline when dependencies change.

## Validation Notes

- Verified the vendored submodule commit locally with `git submodule status` and bootstrapped dependencies via `.devcontainer/setup-vcpkg.sh`.
- Verified the normal macOS contributor path with `cmake -S . -B build -G Ninja`, `cmake --build build --config Release --target yaaf libyaaf_tests`, and `ctest --test-dir build --output-on-failure -L default`.
- Verified a fresh clone path by cloning the repository with `--recurse-submodules`, bootstrapping `./vcpkg`, configuring, and building `yaaf` successfully.
- Hosted CI execution and Linux musl packaging were initially not runnable from this macOS environment because no local Linux container runtime was available.
- Follow-up CI fix: the Linux musl job needs a container image that already has `git` so `actions/checkout` can honor `submodules: recursive` before the dependency-install step runs.
- Follow-up CI fix: `.devcontainer/setup-vcpkg.sh` now assumes the submodule is already initialized and only validates plus bootstraps it, so CI does not need to re-fetch submodules inside the musl container.

## Phase 4 - Documentation And Validation

- [x] Rewrite contributor setup docs for one cross-platform vcpkg flow.
  - [x] Update `docs/usage.md` and any linked README setup text to use clone-with-submodules or `git submodule update --init --recursive` plus local bootstrap commands.
  - [x] Remove instructions that require cloning vcpkg into `$HOME/vcpkg` or exporting `VCPKG_ROOT` as the primary path.
  - [x] Document the repo-maintainer update flow for vcpkg pin bumps.
- [x] Validate the migrated paths.
  - [x] Validate at least one local contributor configure/build flow against the repo-local submodule path.
  - [x] Validate the CI matrix and Linux musl path still configure, build, test, and package successfully with submodule-based setup.
  - [x] Verify a fresh clone without preinstalled vcpkg can follow the documented commands successfully.
