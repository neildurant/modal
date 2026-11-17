// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace modal::platform
{

// The program's arguments as UTF-8, without its own name, from what main() was given. On
// Windows those are in the system's code page, which loses whatever it can't hold, so the
// arguments are asked of the system again instead (found in a code review, November 2026).
std::vector<std::string> arguments(int argc, char* argv[]);

} // namespace modal::platform
