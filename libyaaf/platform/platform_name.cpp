#include "platform_name.h"

namespace yaaf::platform {

std::string platform_name() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "osx";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

}  // namespace yaaf::platform
