// modal: the sequencer.

#include <modal/ui/key.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/test_view.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

int main()
{
    using namespace modal;
    auto opened = ui::sdl::Window::open({.title = "Modal",
                                         .columns = 120,
                                         .rows = 36,
                                         .width = 0,
                                         .height = 0,
                                         .offscreen = false,
                                         .font = {},
                                         .minimumColumns = 80,
                                         .minimumRows = 24});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        ui::sdl::showError("Modal", "Modal " + *error + ".");
        return 1;
    }
    auto& window = *std::get<std::unique_ptr<ui::sdl::Window>>(opened);
    const auto theme = ui::Theme::dark();

    // Until the event list (milestone 4), the test view.
    ui::TestView view;
    window.run({.redraw =
                    [&](ui::sdl::Window& w)
                {
                    view.resize(w.pixelWidth(), w.pixelHeight(), w.cellSize());
                    w.draw(view.frame(), theme);
                },
                .key = [&](const ui::Key& key, std::int64_t) { return view.handle(key); },
                .deadline = {},
                .wake = {}});
    return 0;
}
