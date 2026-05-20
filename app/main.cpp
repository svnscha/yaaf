#include <iostream>

#include "cli/cli.h"
#include "console_utf8.h"

int main(int argc, char **argv)
{
    yaaf::app::configure_console_utf8();
    return yaaf::cli::run(argc, argv, std::cin, std::cout, std::cerr);
}
