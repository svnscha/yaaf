---
description: "Use when editing C++ tests or validating C++ changes in this repository. Covers the covdbg coverage loop, executable discovery, and how to classify remaining uncovered code."
applyTo: "tests/**/*.cpp"
---

# covdbg Validation Loop

- After changing production code or tests, rebuild the affected gtest executable before evaluating coverage.
- In this repository, start coverage work from the real built test binary, usually `http_client_tests`, rather than from CTest.
- Use the repository's `yaaf-covdbg-unit-test-coverage` skill or the equivalent loop: discover the active workspace with `covdbg_explore`, run coverage with `covdbg_run`, inspect file coverage with `covdbg_files`, and inspect uncovered segments with `covdbg_code`.
- Treat coverage as a guide for deciding the next test or refactor, not as a reason to add brittle tests.
- Add tests for reachable public behavior first.
- When an uncovered branch depends on direct third-party failures or other non-deterministic external behavior, prefer creating a seam or documenting it as intentionally defensive instead of forcing unreliable coverage.
- Stop iterating when the touched code is reasonably covered and the remaining uncovered segments are justified as defensive paths, missing seams, or dead code candidates.
