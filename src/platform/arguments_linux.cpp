// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/arguments.hpp>

namespace modal::platform
{

std::vector<std::string> arguments(int argc, char* argv[])
{
    std::vector<std::string> result;
    for (int i = 1; i < argc; ++i)
    {
        result.emplace_back(argv[i]);
    }
    return result;
}

} // namespace modal::platform
