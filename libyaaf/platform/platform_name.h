#pragma once

#include <string>

namespace yaaf::platform
{

/**
 * Get the normalized platform identifier for the current OS.
 * @return One of: "windows", "linux", "osx"
 */
[[nodiscard]] std::string platform_name();

} // namespace yaaf::platform
