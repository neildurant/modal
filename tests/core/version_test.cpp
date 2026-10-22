// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version is the project version")
{
    CHECK(modal::core::version() == "0.1.0");
}
