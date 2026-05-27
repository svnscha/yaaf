#include "process.h"

#include <windows.h>

#include <chrono>
#include <sstream>

namespace yaaf::process
{

/**
 * Helper class to manage environment block for Windows.
 */
class EnvironmentBlock
{
  private:
    std::vector<char> block_;

  public:
    EnvironmentBlock(bool inherit_parent_env, const std::map<std::string, std::string> &overrides)
    {
        std::map<std::string, std::string> env_map;

        // Start with parent environment if requested
        if (inherit_parent_env)
        {
            LPCH parent_env = GetEnvironmentStringsA();
            if (parent_env)
            {
                for (const char *entry = parent_env; *entry != '\0';)
                {
                    std::string env_entry(entry);
                    size_t eq_pos = env_entry.find('=');
                    if (eq_pos != std::string::npos)
                    {
                        std::string key = env_entry.substr(0, eq_pos);
                        std::string value = env_entry.substr(eq_pos + 1);
                        env_map[key] = value;
                    }
                    entry += env_entry.length() + 1;
                }
                FreeEnvironmentStringsA(parent_env);
            }
        }

        // Apply overrides
        for (const auto &[key, value] : overrides)
        {
            env_map[key] = value;
        }

        // Build environment block: KEY=VALUE\0KEY=VALUE\0\0
        for (const auto &[key, value] : env_map)
        {
            block_.insert(block_.end(), key.begin(), key.end());
            block_.push_back('=');
            block_.insert(block_.end(), value.begin(), value.end());
            block_.push_back('\0');
        }
        block_.push_back('\0'); // Final null terminator
    }

    LPVOID get()
    {
        return block_.empty() ? nullptr : block_.data();
    }
};

class WindowsProcess : public PlatformProcess
{
  private:
    HANDLE process_handle_ = nullptr;
    HANDLE stdout_read_handle_ = nullptr;
    HANDLE stdin_write_handle_ = nullptr;

  public:
    WindowsProcess(HANDLE process_handle, HANDLE stdout_read_handle, HANDLE stdin_write_handle)
        : process_handle_(process_handle), stdout_read_handle_(stdout_read_handle),
          stdin_write_handle_(stdin_write_handle)
    {
    }

    ~WindowsProcess() override
    {
        if (stdout_read_handle_ != nullptr)
        {
            CloseHandle(stdout_read_handle_);
        }
        if (stdin_write_handle_ != nullptr)
        {
            CloseHandle(stdin_write_handle_);
        }
        if (process_handle_ != nullptr)
        {
            CloseHandle(process_handle_);
        }
    }

    void write(std::string_view data) override
    {
        if (stdin_write_handle_ == nullptr)
        {
            throw std::runtime_error("write to closed process stdin");
        }

        DWORD total_written = 0;
        while (total_written < data.size())
        {
            DWORD to_write = static_cast<DWORD>(data.size() - total_written);
            DWORD written = 0;

            if (!WriteFile(stdin_write_handle_, data.data() + total_written, to_write, &written, nullptr))
            {
                DWORD err = GetLastError();
                if (err == ERROR_PIPE_NOT_CONNECTED)
                {
                    throw std::runtime_error("child process closed stdin (broken pipe)");
                }
                throw std::runtime_error("failed to write to process stdin: error " + std::to_string(err));
            }

            if (written == 0)
            {
                throw std::runtime_error("write to process stdin returned 0 bytes");
            }

            total_written += written;
        }
    }

    ReadResult read_line(std::chrono::milliseconds timeout) override
    {
        if (stdout_read_handle_ == nullptr)
        {
            return ReadResult{false, true, ""};
        }

        auto start_time = std::chrono::steady_clock::now();
        std::string line;

        while (true)
        {
            // Check if process has exited
            DWORD exit_code = 0;
            if (GetExitCodeProcess(process_handle_, &exit_code))
            {
                if (exit_code != STILL_ACTIVE)
                {
                    // Process has exited
                    return ReadResult{false, true, ""};
                }
            }

            // Calculate remaining timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto remaining = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            if (remaining.count() <= 0)
            {
                return ReadResult{true, false, ""};
            }

            // Read one byte with timeout
            char byte[1];
            DWORD bytes_read = 0;
            DWORD timeout_ms = static_cast<DWORD>(remaining.count());

            // Use WaitForSingleObject to implement timeout
            DWORD wait_result = WaitForSingleObject(stdout_read_handle_, timeout_ms);

            if (wait_result == WAIT_TIMEOUT)
            {
                return ReadResult{true, false, ""};
            }

            if (wait_result != WAIT_OBJECT_0)
            {
                DWORD err = GetLastError();
                throw std::runtime_error("failed to wait on stdout pipe: error " + std::to_string(err));
            }

            // Data is available; read it
            if (!ReadFile(stdout_read_handle_, byte, 1, &bytes_read, nullptr))
            {
                DWORD err = GetLastError();
                if (err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_BROKEN_PIPE)
                {
                    return ReadResult{false, true, ""};
                }
                throw std::runtime_error("failed to read from process stdout: error " + std::to_string(err));
            }

            if (bytes_read == 0)
            {
                // EOF
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
        if (process_handle_ == nullptr)
        {
            return true;
        }

        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process_handle_, &exit_code))
        {
            throw std::runtime_error("failed to check process exit status");
        }

        return exit_code != STILL_ACTIVE;
    }

    void shutdown(std::chrono::milliseconds wait_timeout) override
    {
        if (process_handle_ == nullptr)
        {
            return;
        }

        // Try to wait for graceful exit
        DWORD wait_result = WaitForSingleObject(process_handle_, static_cast<DWORD>(wait_timeout.count()));

        if (wait_result == WAIT_OBJECT_0)
        {
            // Process already exited
            return;
        }

        if (wait_result == WAIT_TIMEOUT)
        {
            // Timeout expired; forcefully terminate
            if (!TerminateProcess(process_handle_, 1))
            {
                throw std::runtime_error("failed to terminate process: error " + std::to_string(GetLastError()));
            }

            // Wait for termination to complete
            WaitForSingleObject(process_handle_, 1000);
        }
        else
        {
            throw std::runtime_error("WaitForSingleObject failed: error " + std::to_string(GetLastError()));
        }
    }
};

/**
 * Quote a command-line argument for Windows.
 * Wraps in quotes and escapes internal quotes.
 */
static std::string quote_arg(const std::string &arg)
{
    // If the argument contains spaces or quotes, we need to quote and escape it
    bool needs_quoting =
        arg.find(' ') != std::string::npos || arg.find('"') != std::string::npos || arg.find('\t') != std::string::npos;

    if (!needs_quoting)
    {
        return arg;
    }

    std::string quoted = "\"";
    for (char c : arg)
    {
        if (c == '"')
        {
            quoted += "\\\"";
        }
        else if (c == '\\')
        {
            // Check if backslash is followed by a quote
            quoted += c;
        }
        else
        {
            quoted += c;
        }
    }
    quoted += "\"";
    return quoted;
}

std::unique_ptr<PlatformProcess> start_process(const ProcessOptions &options)
{
    if (options.command.empty())
    {
        throw std::runtime_error("process command is empty");
    }

    // Create stdout pipe
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, nullptr, 0))
    {
        throw std::runtime_error("failed to create stdout pipe: error " + std::to_string(GetLastError()));
    }

    // Mark stdout_write handle for inheritance by child
    if (!SetHandleInformation(stdout_write, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        throw std::runtime_error("failed to set stdout pipe inheritance");
    }

    // Create stdin pipe
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write, nullptr, 0))
    {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        throw std::runtime_error("failed to create stdin pipe: error " + std::to_string(GetLastError()));
    }

    // Mark stdin_read handle for inheritance by child
    if (!SetHandleInformation(stdin_read, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
    {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        throw std::runtime_error("failed to set stdin pipe inheritance");
    }

    // Build command line: command arg1 arg2 ...
    std::string command_line = quote_arg(options.command);
    for (const auto &arg : options.args)
    {
        command_line += " ";
        command_line += quote_arg(arg);
    }

    // Build environment block
    EnvironmentBlock env_block(options.inherit_parent_env, options.env_overrides);

    // Set up process startup info
    STARTUPINFOA startup_info = {};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdInput = stdin_read;
    startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE); // Inherit stderr from parent

    // Set current directory if specified
    LPCSTR lpCurrentDirectory = nullptr;
    std::string cwd_str;
    if (!options.working_directory.empty())
    {
        cwd_str = options.working_directory.string();
        lpCurrentDirectory = cwd_str.c_str();
    }

    // Create the process
    PROCESS_INFORMATION process_info = {};
    if (!CreateProcessA(nullptr, const_cast<LPSTR>(command_line.c_str()), nullptr, nullptr,
                        TRUE, // bInheritHandles
                        CREATE_NO_WINDOW, env_block.get(), lpCurrentDirectory, &startup_info, &process_info))
    {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);

        std::string error_msg =
            "failed to spawn process: " + options.command + " (error " + std::to_string(GetLastError()) + ")";
        throw std::runtime_error(error_msg);
    }

    // Close child-side handles and thread handle
    CloseHandle(stdout_write);
    CloseHandle(stdin_read);
    CloseHandle(process_info.hThread);

    return std::make_unique<WindowsProcess>(process_info.hProcess, stdout_read, stdin_write);
}

} // namespace yaaf::process
