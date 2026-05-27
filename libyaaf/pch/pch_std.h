#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Safe cross-platform getenv wrapper – silences MSVC C4996 deprecation warning.
namespace yaaf::platform
{
inline const char *safe_getenv(const char *name) noexcept
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
    const char *result = std::getenv(name);
#pragma warning(pop)
    return result;
#else
    return std::getenv(name);
#endif
}
} // namespace yaaf::platform
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
