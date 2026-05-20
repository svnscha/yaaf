---
description: "Use when editing C++ test files in this repository. Covers focused gtest style, behavior-driven assertions, and repository-specific test scope for libyaaf."
applyTo: "tests/**/*.cpp"
---

# C++ Test Guidelines

- Write focused gtest cases that exercise one behavior or failure mode at a time.
- Prefer black-box tests through the public API over tests that depend on internal implementation details.
- Cover the behavior changed by the prompt first, then add nearby cases only when they protect the same contract.
- Name tests after the observable behavior or scenario being verified, not the helper or implementation step used to reach it.
- Keep fixtures and helpers minimal; introduce shared setup only when it removes real duplication without hiding intent.
- Assert the full contract that matters for the scenario, including success or failure state, returned data, and externally visible side effects.
- When validating exceptions, assert both that the exception is thrown and that the triggering behavior is clear from the test setup.
- Avoid tests that try to force non-deterministic third-party failure paths unless the production code already exposes a stable seam for that behavior.
- Prefer adding tests under `tests/` for reachable `libyaaf` behavior rather than expanding app-level coverage through `app/main.cpp`.
