#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yaaf::process {

/**
 * Platform-agnostic options for spawning a child process.
 */
struct ProcessOptions {
    /**
     * Executable path (required).
     * Can be absolute path or executable name to search in PATH.
     */
    std::string command;

    /**
     * Command arguments (default: empty).
     * Does not include argv[0]; passed as array to execve/CreateProcess.
     */
    std::vector<std::string> args;

    /**
     * Working directory override (default: parent's cwd).
     * Should be an absolute path; relative paths are caller's responsibility.
     */
    std::filesystem::path working_directory;

    /**
     * Environment variables to set/override (default: empty).
     * Applied after or instead of parent environment depending on inherit_parent_env.
     */
    std::map<std::string, std::string> env_overrides;

    /**
     * Whether to inherit parent's environment variables (default: true).
     * If true, env_overrides are applied on top of parent's environment.
     * If false, only env_overrides are passed to child.
     */
    bool inherit_parent_env = true;
};

/**
 * Result of reading from stdout with timeout semantics.
 */
struct ReadResult {
    /**
     * True if read timed out before data was available.
     */
    bool timed_out = false;

    /**
     * True if child process exited while waiting for data.
     */
    bool process_exited = false;

    /**
     * Data read from stdout.
     * Contains complete line with trailing newline and CR removed.
     * Empty if timed_out or process_exited is true.
     */
    std::string data;
};

/**
 * Platform-agnostic process handle.
 * Concrete implementation is platform-specific but interface is shared.
 */
class PlatformProcess {
  public:
    virtual ~PlatformProcess() = default;

    /**
     * Write data to child's stdin.
     * Blocks until all data is written or an error occurs.
     *
     * @param data Bytes to write (can include newlines for line-based protocols).
     * @throws std::runtime_error on write failure (e.g., broken pipe if child closed stdin).
     */
    virtual void write(std::string_view data) = 0;

    /**
     * Read one complete newline-delimited line from child's stdout.
     * Reads byte-by-byte until a newline is found, then removes trailing newline and CR.
     * Handles timeouts gracefully without throwing.
     *
     * @param timeout Maximum time to wait before returning. Should not throw; returns timeout flag.
     * @return ReadResult with data, timeout flag, or exit flag.
     * @throws std::runtime_error on unrecoverable I/O error (not on timeout/exit).
     */
    virtual ReadResult read_line(std::chrono::milliseconds timeout) = 0;

    /**
     * Non-blocking check if child process has exited.
     *
     * @return true if process has exited, false if still running.
     * @throws std::runtime_error on wait/query error (rare).
     */
    virtual bool has_exited() const = 0;

    /**
     * Gracefully wait for child process to exit, then forcefully terminate if needed.
     * Safe to call multiple times (second call is no-op if already exited).
     *
     * 1. Wait up to wait_timeout for process to exit naturally.
     * 2. If still running after timeout, send SIGTERM (POSIX) or TerminateProcess (Windows).
     * 3. Wait briefly for termination signal to take effect.
     *
     * @param wait_timeout Time to wait for graceful exit before sending terminate signal.
     * @throws std::runtime_error on termination failure (very rare).
     */
    virtual void shutdown(std::chrono::milliseconds wait_timeout = std::chrono::seconds(1)) = 0;
};

/**
 * Create a platform-specific process.
 * This is the factory function that selects the correct implementation for the current OS.
 *
 * @param options Process launch options (command is required).
 * @return Platform-specific process handle ready for I/O.
 * @throws std::runtime_error if:
 *   - command is empty
 *   - pipes cannot be created (resource exhaustion, permissions)
 *   - spawn fails (executable not found, permission denied, OS limit reached)
 */
[[nodiscard]] std::unique_ptr<PlatformProcess> start_process(const ProcessOptions& options);

} // namespace yaaf::process
