#include "mcp_client_stdio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace yaaf::mcp::detail
{
namespace
{
class HandleGuard
{
  public:
    explicit HandleGuard(HANDLE handle = nullptr) : handle_(handle)
    {
    }

    ~HandleGuard()
    {
        reset();
    }

    HandleGuard(const HandleGuard &) = delete;
    HandleGuard &operator=(const HandleGuard &) = delete;

    HandleGuard(HandleGuard &&other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    HandleGuard &operator=(HandleGuard &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const
    {
        return handle_;
    }

    void reset(HANDLE handle = nullptr)
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_ = nullptr;
};

[[nodiscard]] std::optional<std::string> getenv_string(const std::string &name)
{
    std::vector<char> buffer(32767);
    SetLastError(ERROR_SUCCESS);
    const auto written = GetEnvironmentVariableA(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0)
    {
        return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>{std::string{}};
    }
    if (written >= buffer.size())
    {
        return std::nullopt;
    }
    return std::string(buffer.data(), written);
}

class EnvironmentOverrideGuard
{
  public:
    explicit EnvironmentOverrideGuard(const nlohmann::json &raw)
    {
        for (const auto &[name, value] : read_environment_overrides(raw))
        {
            previous_values_[name] = getenv_string(name);
            SetEnvironmentVariableA(name.c_str(), value.c_str());
        }
    }

    ~EnvironmentOverrideGuard()
    {
        for (const auto &[name, previous_value] : previous_values_)
        {
            SetEnvironmentVariableA(name.c_str(), previous_value ? previous_value->c_str() : nullptr);
        }
    }

    EnvironmentOverrideGuard(const EnvironmentOverrideGuard &) = delete;
    EnvironmentOverrideGuard &operator=(const EnvironmentOverrideGuard &) = delete;

  private:
    std::map<std::string, std::optional<std::string>> previous_values_;
};

[[nodiscard]] std::string quote_command_part(const std::string &value)
{
    if (value.find_first_of(" \t\"") == std::string::npos)
    {
        return value;
    }

    std::string quoted = "\"";
    for (const char character : value)
    {
        if (character == '\"')
        {
            quoted += "\\\"";
        }
        else
        {
            quoted += character;
        }
    }
    quoted += '"';
    return quoted;
}

class Win32StdioProcess final : public StdioPlatformProcess
{
  public:
    explicit Win32StdioProcess(const nlohmann::json &raw)
    {
        const auto command = json_string_value(raw, "command");
        if (command.empty())
        {
            throw std::runtime_error("stdio MCP server requires command");
        }

        std::string command_line = quote_command_part(command);
        if (const auto args = raw.find("args"); args != raw.end() && args->is_array())
        {
            for (const auto &arg : *args)
            {
                if (arg.is_string())
                {
                    command_line += ' ';
                    command_line += quote_command_part(arg.get<std::string>());
                }
            }
        }

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
        security_attributes.bInheritHandle = TRUE;

        HANDLE stdout_read = nullptr;
        HANDLE stdout_write = nullptr;
        HANDLE stdin_read = nullptr;
        HANDLE stdin_write = nullptr;
        if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0) ||
            !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
            !CreatePipe(&stdin_read, &stdin_write, &security_attributes, 0) ||
            !SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0))
        {
            throw std::runtime_error("failed to create MCP stdio pipes");
        }

        stdout_read_.reset(stdout_read);
        HandleGuard stdout_write_guard(stdout_write);
        HandleGuard stdin_read_guard(stdin_read);
        stdin_write_.reset(stdin_write);

        STARTUPINFOA startup_info{};
        startup_info.cb = sizeof(STARTUPINFOA);
        startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startup_info.hStdOutput = stdout_write_guard.get();
        startup_info.hStdInput = stdin_read_guard.get();
        startup_info.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION process_info{};
        std::vector<char> mutable_command_line(command_line.begin(), command_line.end());
        mutable_command_line.push_back('\0');

        EnvironmentOverrideGuard environment_guard{raw};
        if (!CreateProcessA(nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &startup_info, &process_info))
        {
            throw std::runtime_error(fmt::format("failed to start MCP stdio server: {}", command));
        }

        process_.reset(process_info.hProcess);
        thread_.reset(process_info.hThread);
    }

    ~Win32StdioProcess() override
    {
        stdin_write_.reset();
        if (process_.get() != nullptr)
        {
            WaitForSingleObject(process_.get(), 1000);
        }
    }

    void write_message(std::string_view line) override
    {
        DWORD written = 0;
        if (!WriteFile(stdin_write_.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
            written != line.size())
        {
            throw std::runtime_error("failed to write MCP message to stdio server");
        }
    }

    [[nodiscard]] nlohmann::json read_message(std::chrono::milliseconds timeout) override
    {
        std::string line;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(stdout_read_.get(), nullptr, 0, nullptr, &available, nullptr))
            {
                throw std::runtime_error("failed to read MCP stdio pipe");
            }
            if (available == 0)
            {
                if (WaitForSingleObject(process_.get(), 10) == WAIT_OBJECT_0)
                {
                    throw std::runtime_error("MCP stdio server exited before replying");
                }
                Sleep(10);
                continue;
            }

            char character = '\0';
            DWORD read = 0;
            if (!ReadFile(stdout_read_.get(), &character, 1, &read, nullptr) || read != 1)
            {
                throw std::runtime_error("failed to read MCP message from stdio server");
            }
            if (character == '\n')
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                return nlohmann::json::parse(line);
            }
            line += character;
        }

        throw std::runtime_error("timed out waiting for MCP stdio response");
    }

  private:
    HandleGuard process_;
    HandleGuard thread_;
    HandleGuard stdin_write_;
    HandleGuard stdout_read_;
};
} // namespace

std::unique_ptr<StdioPlatformProcess> start_stdio_server(const nlohmann::json &raw)
{
    return std::make_unique<Win32StdioProcess>(raw);
}
} // namespace yaaf::mcp::detail
