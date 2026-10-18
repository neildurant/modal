// modal: the sequencer.

#include <modal/ui/sdl/window.hpp>

#include <memory>
#include <string>
#include <variant>

namespace
{

using namespace modal;

constexpr ui::sdl::Colour kBackground{0x1a, 0x1b, 0x26};
constexpr ui::sdl::Colour kText{0xc0, 0xca, 0xf5};

// Until the test view (milestone 3, step 4): a sample of the characters the views will use.
constexpr const char* kSample[] = {
    "Modal: the quick brown fox, 0123456789",
    "17:2:480 │ E3 (52) │ vel 96 │ dur 0:1:240 (1/8.) │ F♯3 B♭2",
    "",
    "┌───┬───┐ ┏━━━┳━━━┓ ┍━━━┯━━━┑   shades    ░▒▓█",
    "│ 1 │ 2 │ ┃ 1 ┃ 2 ┃ │ 1 │ 2 │   eighths   ▁▂▃▄▅▆▇█ ▏▎▍▌▋▊▉█",
    "├───┼───┤ ┣━━━╋━━━┫ ┝━━━┿━━━┥   halves    ▀▄▌▐ ▔▕",
    "└───┴───┘ ┗━━━┻━━━┛ ┕━━━┷━━━┙   quadrants ▖▗▘▝ ▙▚▛▜▝▞▟",
    "",
    "density   ░░▒▒▓▓██▓▓▒▒░░  ▁▂▃▅▇█▇▅▃▂▁",
    "arrows    ←→↑↓ ▶◀▲▼ ●○ ·•…×±≤≥",
    "font's own (not drawn): ═║╔╗╚╝ ╭╮╰╯ ┄┆ ╱╲",
    "not in the font, so shown as a replacement: ♮♩",
    "ASCII     {}[]()<>|\\/\"'`~!@#$%^&*_+-=",
};

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
    window.run(
        [](ui::sdl::Window& w)
        {
            w.drawFrame(kBackground,
                        [&]
                        {
                            int row = 1;
                            for (const auto* line : kSample)
                            {
                                w.drawText(1, row++, line, kText);
                            }
                        });
        });
    return 0;
}
