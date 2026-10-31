// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/paths.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

using namespace modal::platform;

TEST_CASE("the config directory is Modal's own, under the user's")
{
    const auto directory = configDirectory();
    REQUIRE(directory);
    CHECK(directory->is_absolute());
#ifdef _WIN32
    CHECK(directory->filename() == "Modal");
#else
    CHECK(directory->filename() == "modal");
#endif
}

TEST_CASE("the state directory is Modal's own, apart from the config directory")
{
    const auto directory = stateDirectory();
    REQUIRE(directory);
    CHECK(directory->is_absolute());
    CHECK(directory != configDirectory());
#ifdef _WIN32
    CHECK(directory->filename() == "Modal");
#else
    CHECK(directory->filename() == "modal");
#endif
}

#ifndef _WIN32
TEST_CASE("on Linux the state directory follows XDG_STATE_HOME, if it's absolute")
{
    const char* old = std::getenv("XDG_STATE_HOME");
    const std::string saved = old != nullptr ? old : "";

    setenv("XDG_STATE_HOME", "/tmp/modal-test-state", 1);
    CHECK(stateDirectory() == std::filesystem::path{"/tmp/modal-test-state/modal"});
    setenv("XDG_STATE_HOME", "relative", 1); // ignored
    CHECK(stateDirectory()->parent_path() ==
          std::filesystem::path{std::getenv("HOME")} / ".local" / "state");

    if (old != nullptr)
    {
        setenv("XDG_STATE_HOME", saved.c_str(), 1);
    }
    else
    {
        unsetenv("XDG_STATE_HOME");
    }
}

TEST_CASE("on Linux the config directory follows XDG_CONFIG_HOME, if it's absolute")
{
    const char* old = std::getenv("XDG_CONFIG_HOME");
    const std::string saved = old != nullptr ? old : "";

    setenv("XDG_CONFIG_HOME", "/tmp/modal-test-config", 1);
    CHECK(configDirectory() == std::filesystem::path{"/tmp/modal-test-config/modal"});
    setenv("XDG_CONFIG_HOME", "relative", 1); // ignored
    CHECK(configDirectory()->parent_path().filename() == ".config");

    if (old != nullptr)
    {
        setenv("XDG_CONFIG_HOME", saved.c_str(), 1);
    }
    else
    {
        unsetenv("XDG_CONFIG_HOME");
    }
}
#endif
