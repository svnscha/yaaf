#pragma once

#include "../pch/pch_std.h"

namespace yaaf::platform
{
/**
 * Returns the absolute filesystem path of the currently running executable.
 *
 * Resolution is platform-specific:
 * - Windows: `GetModuleFileNameW(nullptr, ...)`.
 * - Linux: `/proc/self/exe` via `readlink`.
 * - macOS: `_NSGetExecutablePath`.
 *
 * @return Absolute path to the running executable, or an empty path if the platform
 *         lookup fails. Callers must treat an empty result as "unknown" and fall back
 *         to a sensible default.
 */
[[nodiscard]] std::filesystem::path executable_path();

/**
 * Returns the absolute directory that contains the currently running executable.
 *
 * The yaaf build copies bundled runtime assets (`lua/`, `examples/`) into this directory,
 * so callers use it to resolve those assets relative to the deployed binary layout instead
 * of the caller's current working directory.
 *
 * @return Absolute directory of the running executable, or an empty path when
 *         `executable_path()` cannot be determined.
 */
[[nodiscard]] std::filesystem::path executable_directory();
} // namespace yaaf::platform
