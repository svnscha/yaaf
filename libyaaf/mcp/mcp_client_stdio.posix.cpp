#include "mcp_client_stdio.h"

#include <cerrno>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace yaaf::mcp::detail
{
namespace
{
class FdGuard
{
  public:
    explicit FdGuard(int fd = -1) : fd_(fd)
    {
    }

    ~FdGuard()
    {
        reset();
    }

    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;

    FdGuard(FdGuard &&other) noexcept : fd_(std::exchange(other.fd_, -1))
    {
    }

    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const
    {
        return fd_;
    }

    [[nodiscard]] int release()
    {
        return std::exchange(fd_, -1);
    }

    void reset(int fd = -1)
    {
        if (fd_ >= 0)
        {
            close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_ = -1;
};

[[nodiscard]] std::vector<std::string> build_environment(const nlohmann::json &raw)
{
    std::map<std::string, std::string> values;
    if (environ != nullptr)
    {
        for (char **entry = environ; *entry != nullptr; ++entry)
        {
            const std::string_view variable{*entry};
            const auto separator = variable.find('=');
            if (separator == std::string_view::npos || separator == 0)
            {
                continue;
            }
            values[std::string(variable.substr(0, separator))] = std::string(variable.substr(separator + 1));
        }
    }

    for (const auto &[name, value] : read_environment_overrides(raw))
    {
        values[name] = value;
    }

    std::vector<std::string> environment;
    environment.reserve(values.size());
    for (const auto &[name, value] : values)
    {
        environment.push_back(name + "=" + value);
    }
    return environment;
}

[[nodiscard]] bool process_exited(pid_t process)
{
    if (process <= 0)
    {
        return true;
    }

    int status = 0;
    const auto result = waitpid(process, &status, WNOHANG);
    if (result == process)
    {
        return true;
    }
    if (result == -1 && errno == ECHILD)
    {
        return true;
    }
    return false;
}

[[nodiscard]] bool wait_for_process_exit(pid_t process, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (process_exited(process))
        {
            return true;
        }
        poll(nullptr, 0, 10);
    }
    return process_exited(process);
}

class PosixStdioProcess final : public StdioPlatformProcess
{
  public:
    explicit PosixStdioProcess(const nlohmann::json &raw)
    {
        const auto command = json_string_value(raw, "command");
        if (command.empty())
        {
            throw std::runtime_error("stdio MCP server requires command");
        }

        std::vector<std::string> argv_storage;
        argv_storage.push_back(command);
        if (const auto args = raw.find("args"); args != raw.end() && args->is_array())
        {
            for (const auto &arg : *args)
            {
                if (arg.is_string())
                {
                    argv_storage.push_back(arg.get<std::string>());
                }
            }
        }

        std::vector<char *> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto &arg : argv_storage)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        auto environment_storage = build_environment(raw);
        std::vector<char *> environment;
        environment.reserve(environment_storage.size() + 1);
        for (auto &entry : environment_storage)
        {
            environment.push_back(entry.data());
        }
        environment.push_back(nullptr);

        int stdout_pipe[2] = {-1, -1};
        int stdin_pipe[2] = {-1, -1};
        if (pipe(stdout_pipe) != 0 || pipe(stdin_pipe) != 0)
        {
            if (stdout_pipe[0] >= 0)
            {
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
            }
            if (stdin_pipe[0] >= 0)
            {
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);
            }
            throw std::runtime_error("failed to create MCP stdio pipes");
        }

        FdGuard stdout_read{stdout_pipe[0]};
        FdGuard stdout_write{stdout_pipe[1]};
        FdGuard stdin_read{stdin_pipe[0]};
        FdGuard stdin_write{stdin_pipe[1]};

        posix_spawn_file_actions_t file_actions{};
        posix_spawn_file_actions_init(&file_actions);
        posix_spawn_file_actions_adddup2(&file_actions, stdin_read.get(), STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&file_actions, stdout_write.get(), STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&file_actions, stdin_write.get());
        posix_spawn_file_actions_addclose(&file_actions, stdout_read.get());

        pid_t process = -1;
        const auto spawn_result =
            posix_spawnp(&process, command.c_str(), &file_actions, nullptr, argv.data(), environment.data());
        posix_spawn_file_actions_destroy(&file_actions);
        if (spawn_result != 0)
        {
            throw std::runtime_error(fmt::format("failed to start MCP stdio server: {}", command));
        }

        stdout_read_.reset(stdout_read.release());
        stdin_write_.reset(stdin_write.release());
        process_ = process;
    }

    ~PosixStdioProcess() override
    {
        stdin_write_.reset();
        stdout_read_.reset();
        if (process_ > 0 && !wait_for_process_exit(process_, std::chrono::seconds(1)))
        {
            kill(process_, SIGTERM);
            waitpid(process_, nullptr, 0);
        }
    }

    void write_message(std::string_view line) override
    {
        std::size_t written = 0;
        while (written < line.size())
        {
            const auto count = write(stdin_write_.get(), line.data() + written, line.size() - written);
            if (count > 0)
            {
                written += static_cast<std::size_t>(count);
                continue;
            }
            if (count == -1 && errno == EINTR)
            {
                continue;
            }
            throw std::runtime_error("failed to write MCP message to stdio server");
        }
    }

    [[nodiscard]] nlohmann::json read_message(std::chrono::milliseconds timeout) override
    {
        std::string line;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            pollfd descriptor{stdout_read_.get(), POLLIN | POLLHUP, 0};
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            const auto poll_timeout =
                static_cast<int>(std::max<std::int64_t>(1, std::min<std::int64_t>(remaining.count(), 100)));
            const auto ready = poll(&descriptor, 1, poll_timeout);
            if (ready == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                throw std::runtime_error("failed to read MCP stdio pipe");
            }
            if (ready == 0)
            {
                if (process_exited(process_))
                {
                    throw std::runtime_error("MCP stdio server exited before replying");
                }
                continue;
            }

            char character = '\0';
            const auto count = read(stdout_read_.get(), &character, 1);
            if (count == 1)
            {
                if (character == '\n')
                {
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    return nlohmann::json::parse(line);
                }
                line += character;
                continue;
            }
            if (count == -1 && errno == EINTR)
            {
                continue;
            }
            if (count == 0 && process_exited(process_))
            {
                throw std::runtime_error("MCP stdio server exited before replying");
            }
            throw std::runtime_error("failed to read MCP message from stdio server");
        }

        throw std::runtime_error("timed out waiting for MCP stdio response");
    }

  private:
    pid_t process_ = -1;
    FdGuard stdin_write_;
    FdGuard stdout_read_;
};
} // namespace

std::unique_ptr<StdioPlatformProcess> start_stdio_server(const nlohmann::json &raw)
{
    return std::make_unique<PosixStdioProcess>(raw);
}
} // namespace yaaf::mcp::detail
