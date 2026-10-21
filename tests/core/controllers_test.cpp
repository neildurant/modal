#include <modal/core/controllers.hpp>

#include <catch2/catch_test_macros.hpp>

using modal::core::controllerName;

TEST_CASE("controllers have their standard names")
{
    CHECK(controllerName(1) == "Modulation");
    CHECK(controllerName(7) == "Volume");
    CHECK(controllerName(39) == "Volume LSB");
    CHECK(controllerName(64) == "Sustain");
    CHECK(controllerName(74) == "Brightness");
    CHECK(controllerName(123) == "All Notes Off");
    CHECK(controllerName(3).empty());
    CHECK(controllerName(35).empty()); // the low half of 3, which has no name
    CHECK(controllerName(200).empty());
}
