#include "console_utf8.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace yaaf::app
{
void configure_console_utf8()
{
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}
} // namespace yaaf::app
