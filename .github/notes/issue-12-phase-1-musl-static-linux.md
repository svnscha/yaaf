# Issue 12 Phase 1: musl static Linux build path

## Decision

- Toolchain: a native musl Linux build environment, expected to be an Alpine-based container or runner in later phases.
- vcpkg triplet: `x64-linux-musl-static`, provided by the repository overlay at `triplets/x64-linux-musl-static.cmake`.
- Static-link target: static third-party libraries from vcpkg, producing a musl-oriented Linux release configuration without changing the existing Windows or macOS paths.

## Repository entry points added in Phase 1

- `CMakePresets.json` adds an opt-in `linux-musl-static` configure preset.
- `triplets/x64-linux-musl-static.cmake` adds the repository-owned triplet overlay.

The preset intentionally does not change default configure behavior. Existing Windows, Ubuntu, and macOS builds continue to use their current commands.

## Dependency-resolution check

The Phase 1 validation used a manifest dry run so the repo can verify package resolution before introducing the full musl CI environment.

Command:

```sh
./vcpkg/vcpkg install --dry-run \
  --triplet x64-linux-musl-static \
  --overlay-triplets=triplets
```

Resolved direct dependencies:

- `cli11`
- `curl`
- `fmt`
- `gtest`
- `lua`
- `nlohmann-json`

Resolved transitive dependencies observed in the dry run:

- `openssl` via `curl`
- `zlib` via `curl`
- `vcpkg-cmake`
- `vcpkg-cmake-config`
- `vcpkg-cmake-get-vars`

## Current local build path

The repository devcontainer now switches to an Alpine-based musl-native toolchain so contributors can exercise the `linux-musl-static` preset locally after rebuilding the container.

The remaining validation gap is CI coverage: the full `cmake --preset linux-musl-static` configure/build and runtime smoke matrix still need to move into the repository workflow before the Linux release path can be considered migrated.