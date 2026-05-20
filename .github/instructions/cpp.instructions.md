---
description: "Use when editing C++ source or headers in this repository. Covers shared PCH usage, include discipline, API documentation, and behavior-focused Doxygen comments."
applyTo: "**/*.cpp,**/*.h"
---

# C++ Repository Guidelines

- Treat `libyaaf/pch/pch_std.h` and `libyaaf/pch/pch_dependencies.h` as the shared precompiled headers for C++ targets in this repository.
- Do not add direct includes for standard library headers already expected from `libyaaf/pch/pch_std.h`.
- Do not add direct includes for `fmt/format.h` or `nlohmann/json.hpp`; they are provided through `libyaaf/pch/pch_dependencies.h`.
- Include only headers that are not covered by the precompiled headers, such as project headers or `CLI/CLI.hpp`.
- Prefer the `std::unique_ptr` PIMPL pattern for non-trivial classes so public headers stay light, forward declarations remain effective, and include chains stay clean.
- For cross-platform behavior, keep the public API in one shared header and split OS-specific code into sibling translation units such as `*.win32.cpp` and `*.posix.cpp`, following the `app/console_utf8.*` pattern.
- Prefer build-selected platform files over scattering `#ifdef` branches through shared headers or call sites; keep platform conditionals local to the implementation file that actually needs them.
- When one platform does not need special handling, provide the same function in that platform file as a small no-op or platform-appropriate implementation rather than introducing a separate API shape.
- Use Doxygen-style comments for public types and functions whose behavior is not obvious from the signature.
- Document behavior, not implementation trivia: preconditions, postconditions, ownership, lifetime expectations, side effects, and notable state changes.
- When a function can fail by throwing, document that explicitly with `@throws`, including the condition that triggers the exception when it is known.
- When return values carry important semantics such as ownership transfer, nullability, error state, or partial success, document that in `@return`.
- Keep comments concise and factual; avoid boilerplate comments that restate the function name or parameter names without adding behavioral meaning.
