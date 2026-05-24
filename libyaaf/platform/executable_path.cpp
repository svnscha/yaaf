#include "executable_path.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace yaaf::platform
{
namespace
{
#if defined(_WIN32)
[[nodiscard]] std::filesystem::path resolve_executable_path()
{
    std::wstring buffer;
    buffer.resize(MAX_PATH);

    for (;;)
    {
        const auto length =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < buffer.size())
        {
            buffer.resize(length);
            std::error_code ignored;
            auto path = std::filesystem::path{buffer};
            auto canonical = std::filesystem::weakly_canonical(path, ignored);
            return ignored ? path : canonical;
        }

        // Buffer was too small; grow and try again.
        buffer.resize(buffer.size() * 2);
    }
}
#elif defined(__APPLE__)
[[nodiscard]] std::filesystem::path resolve_executable_path()
{
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return {};
    }

    std::error_code ignored;
    auto path = std::filesystem::path{buffer.c_str()};
    auto canonical = std::filesystem::weakly_canonical(path, ignored);
    return ignored ? path : canonical;
}
#else
[[nodiscard]] std::filesystem::path resolve_executable_path()
{
    std::error_code ec;
    auto link = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
    {
        return {};
    }

    auto canonical = std::filesystem::weakly_canonical(link, ec);
    return ec ? link : canonical;
}
#endif
} // namespace

std::filesystem::path executable_path()
{
    return resolve_executable_path();
}

std::filesystem::path executable_directory()
{
    auto path = executable_path();
    if (path.empty())
    {
        return {};
    }
    return path.parent_path();
}
} // namespace yaaf::platform
