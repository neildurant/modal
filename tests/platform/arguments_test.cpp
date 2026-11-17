// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/arguments.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <vector>

using namespace modal::platform;

#ifdef _WIN32
TEST_CASE("on Windows the arguments come from the system, whatever main() was given")
{
    // main()'s are in the code page; these bytes aren't UTF-8, and must not be taken as it.
    char name[] = "modal";
    char file[] = "Caf\xE9.modal";
    char* given[] = {name, file};
    const auto found = arguments(2, given);
    CHECK(found.size() == static_cast<std::size_t>(__argc - 1)); // the test program's own
    for (const auto& argument : found)
    {
        CHECK(argument != file);
    }
}
#else
TEST_CASE("the arguments leave out the program's name")
{
    char name[] = "modal";
    char demo[] = "--demo";
    char file[] = "Caf\xC3\xA9.modal";
    char* given[] = {name, demo, file};
    CHECK(arguments(3, given) == std::vector<std::string>{"--demo", "Caf\xC3\xA9.modal"});
    CHECK(arguments(1, given).empty());
}
#endif
