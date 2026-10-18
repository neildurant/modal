#include <modal/ui/sdl/window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <variant>

using namespace modal::ui::sdl;

TEST_CASE("an offscreen window draws frames that can be read back")
{
    auto opened = Window::open({.title = "test", .width = 64, .height = 48, .offscreen = true});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        FAIL(*error);
    }
    auto& window = *std::get<std::unique_ptr<Window>>(opened);
    CHECK(window.pixelWidth() >= 64);
    CHECK(window.pixelHeight() >= 48);
    CHECK(window.pixel(0, 0) == std::nullopt); // nothing drawn yet

    const Colour background{0x12, 0x34, 0x56};
    window.drawFrame(background);
    CHECK(window.pixel(0, 0) == background);
    CHECK(window.pixel(window.pixelWidth() - 1, window.pixelHeight() - 1) == background);
    CHECK(window.pixel(window.pixelWidth(), 0) == std::nullopt);
}
