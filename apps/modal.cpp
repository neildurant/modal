// modal: the sequencer.

#include <modal/ui/sdl/window.hpp>

#include <memory>
#include <string>
#include <variant>

namespace
{

using namespace modal;

constexpr ui::sdl::Colour kBackground{0x1a, 0x1b, 0x26};

} // namespace

int main()
{
    auto opened = ui::sdl::Window::open({});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        ui::sdl::showError("Modal", "Modal " + *error + ".");
        return 1;
    }
    auto& window = *std::get<std::unique_ptr<ui::sdl::Window>>(opened);
    window.run([](ui::sdl::Window& w) { w.drawFrame(kBackground); });
    return 0;
}
