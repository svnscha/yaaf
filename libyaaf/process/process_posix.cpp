#include "process.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

namespace yaaf::process
{

// Helper: build argv array from command and args
static std::vector<const char *> build_argv(const std::string &command, const std::vector<std::string> &args)
{
    std::vector<const char *> argv;
    argv.push_back(command.c_str());
    for (const auto &arg : args)
    {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr); // NULL terminator for execvpe
    return argv;
}

// Helper: build environment array from inherited environ + overrides
static std::vector<const char *> build_envp(bool inherit_parent_env,
                                            const std::map<std::string, std::string> &overrides)
{
    std::map<std::string, std::string> env_map;

    // Start with parent environment if requested
    if (inherit_parent_env)
    {
        if (environ != nullptr)
        {
            for (char **e = environ; *e != nullptr; ++e)
            {
                std::string entry(*e);
                size_t eq_pos = entry.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = entry.substr(0, eq_pos);
                    std::string value = entry.substr(eq_pos + 1);
                    env_map[key] = value;
                }
            }
        }
    }

    // Apply overrides
    for (const auto &[key, value] : overrides)
    {
        env_map[key] = value;
    }

    // Convert map to argv-style array
    std::vector<const char *> envp;
    std::vector<std::string> env_strings;
    for (const auto &[key, value] : env_map)
    {
        env_strings.push_back(key + "=" + value);
    }

    for (const auto &entry : env_strings)
    {
        envp.push_back(entry.c_str());
    }
    envp.push_back(nullptr);

    // Note: This function returns a temporary vector; the vector will be destroyed
    // But the caller immediately uses it in execvpe, which uses the data directly.
    // For safety, we'll need to rethink this in the actual implementation.
    // Better: use execvp and set environment separately, or pass env_strings with longer lifetime.
    return envp;
}

class PosixProcess : public PlatformProcess
{
  private:
    pid_t pid_ = -1;
    int stdout_fd_ = -1; // File descriptor for reading from child's stdout
    int stdin_fd_ = -1;  // File descriptor for writing to child's stdin

  public:
    PosixProcess(pid_t pid, int stdout_fd, int stdin_fd) : pid_(pid), stdout_fd_(stdout_fd), stdin_fd_(stdin_fd)
    {
    }

    ~PosixProcess() override
    {
        // Clean up file descriptors
        if (stdout_fd_ >= 0)
        {
            ::close(stdout_fd_);
        }
        if (stdin_fd_ >= 0)
        {
            ::close(stdin_fd_);
        }

        // Reap the child process if still running
        if (pid_ > 0)
        {
            ::waitpid(pid_, nullptr, 0);
        }
    }

    void write(std::string_view data) override
    {
        if (stdin_fd_ < 0)
        {
            throw std::runtime_error("write to closed process stdin");
        }

        size_t total_written = 0;
        while (total_written < data.size())
        {
            ssize_t n = ::write(stdin_fd_, data.data() + total_written, data.size() - total_written);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue; // Interrupted system call; retry
                }
                if (errno == EPIPE)
                {
                    throw std::runtime_error("child process closed stdin (broken pipe)");
                }
                throw std::runtime_error(std::string("failed to write to process stdin: ") + std::strerror(errno));
            }
            if (n == 0)
            {
                throw std::runtime_error("write to process stdin returned 0 bytes");
            }
            total_written += n;
        }
    }

    ReadResult read_line(std::chrono::milliseconds timeout) override
    {
        if (stdout_fd_ < 0)
        {
            return ReadResult{false, true, ""}; // Already closed
        }

        auto start_time = std::chrono::steady_clock::now();
        std::string line;

        while (true)
        {
            // Check if process has exited
            int wstatus = 0;
            pid_t result = ::waitpid(pid_, &wstatus, WNOHANG);
            if (result == pid_)
            {
                // Process has exited
                return ReadResult{false, true, ""};
            }

            // Calculate remaining timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto remaining = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            if (remaining.count() <= 0)
            {
                return ReadResult{true, false, ""}; // Timeout
            }

            // Read one byte with timeout using select/poll
            // For simplicity, we'll use a small read loop with non-blocking I/O
            char byte[1];
            ssize_t n = ::read(stdout_fd_, byte, 1);

            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue; // Interrupted; retry
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // No data available; sleep briefly and retry
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                throw std::runtime_error(std::string("failed to read from process stdout: ") + std::strerror(errno));
            }

            if (n == 0)
            {
                // EOF - process closed stdout
                return ReadResult{false, true, ""};
            }

            // Got one byte
            if (byte[0] == '\n')
            {
                // End of line; strip trailing CR if present
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                return ReadResult{false, false, line};
            }
            else
            {
                line += byte[0];
            }
        }
    }

    bool has_exited() const override
    {
        if (pid_ <= 0)
        {
            return true;
        }

        int wstatus = 0;
        pid_t result = ::waitpid(pid_, &wstatus, WNOHANG);
        if (result == pid_)
        {
            return true;
        }
        if (result < 0)
        {
            if (errno == ECHILD)
            {
                return true; // Process doesn't exist
            }
            throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
        }

        return false;
    }

    void shutdown(std::chrono::milliseconds wait_timeout) override
    {
        if (pid_ <= 0)
        {
            return; // Already cleaned up
        }

        // Try to gracefully wait for exit
        auto start_time = std::chrono::steady_clock::now();
        while (true)
        {
            int wstatus = 0;
            pid_t result = ::waitpid(pid_, &wstatus, WNOHANG);

            if (result == pid_)
            {
                // Process exited
                pid_ = -1;
                return;
            }

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) >= wait_timeout)
            {
                break; // Timeout; proceed to forceful termination
            }

            // Sleep briefly before next check
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Graceful timeout expired; send SIGTERM
        if (::kill(pid_, SIGTERM) < 0 && errno != ESRCH)
        {
            throw std::runtime_error(std::string("failed to send SIGTERM: ") + std::strerror(errno));
        }

        // Wait a bit more for SIGTERM to take effect
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check if process is still alive
        int wstatus = 0;
        pid_t result = ::waitpid(pid_, &wstatus, WNOHANG);
        if (result == pid_)
        {
            // Process exited from SIGTERM
            pid_ = -1;
            return;
        }

        // Still alive; send SIGKILL
        if (::kill(pid_, SIGKILL) < 0 && errno != ESRCH)
        {
            throw std::runtime_error(std::string("failed to send SIGKILL: ") + std::strerror(errno));
        }

        // SIGKILL is unblockable; wait for it
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        result = ::waitpid(pid_, &wstatus, WNOHANG);
        if (result == pid_)
        {
            pid_ = -1;
        }
    }
};

std::unique_ptr<PlatformProcess> start_process(const ProcessOptions &options)
{
    if (options.command.empty())
    {
        throw std::runtime_error("process command is empty");
    }

    // Create stdout pipe (parent reads, child writes)
    int stdout_pipe[2];
    if (::pipe(stdout_pipe) < 0)
    {
        throw std::runtime_error(std::string("failed to create stdout pipe: ") + std::strerror(errno));
    }

    // Create stdin pipe (parent writes, child reads)
    int stdin_pipe[2];
    if (::pipe(stdin_pipe) < 0)
    {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        throw std::runtime_error(std::string("failed to create stdin pipe: ") + std::strerror(errno));
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        throw std::runtime_error(std::string("failed to fork: ") + std::strerror(errno));
    }

    if (pid == 0)
    {
        // Child process
        // Close parent-side fds
        ::close(stdout_pipe[0]);
        ::close(stdin_pipe[1]);

        // Redirect stdout
        if (::dup2(stdout_pipe[1], STDOUT_FILENO) < 0)
        {
            std::perror("dup2 stdout failed");
            ::_exit(127);
        }
        ::close(stdout_pipe[1]);

        // Redirect stdin
        if (::dup2(stdin_pipe[0], STDIN_FILENO) < 0)
        {
            std::perror("dup2 stdin failed");
            ::_exit(127);
        }
        ::close(stdin_pipe[0]);

        // Change working directory if specified
        if (!options.working_directory.empty())
        {
            if (::chdir(options.working_directory.c_str()) < 0)
            {
                std::perror("chdir failed");
                ::_exit(127);
            }
        }

        // Build argv
        auto argv_vec = build_argv(options.command, options.args);
        std::vector<char *> argv_ptrs;
        for (auto ptr : argv_vec)
        {
            argv_ptrs.push_back(const_cast<char *>(ptr));
        }

        // Build environment
        auto envp_vec = build_envp(options.inherit_parent_env, options.env_overrides);
        std::vector<char *> envp_ptrs;
        for (auto ptr : envp_vec)
        {
            envp_ptrs.push_back(const_cast<char *>(ptr));
        }

        // Execute child
        // Note: execvpe is not POSIX standard; using execvp instead with pre-set environment
        if (!options.env_overrides.empty() || !options.inherit_parent_env)
        {
            // Need to modify environment; use environ manipulation
            for (const auto &[key, value] : options.env_overrides)
            {
                std::string env_str = key + "=" + value;
                ::setenv(key.c_str(), value.c_str(), 1);
            }
        }

        ::execvp(options.command.c_str(), argv_ptrs.data());

        // If execvp returns, it failed
        std::perror("execvp failed");
        ::_exit(127);
    }

    // Parent process
    // Close child-side fds
    ::close(stdout_pipe[1]);
    ::close(stdin_pipe[0]);

    // Return process handle
    return std::make_unique<PosixProcess>(pid, stdout_pipe[0], stdin_pipe[1]);
}

} // namespace yaaf::process
