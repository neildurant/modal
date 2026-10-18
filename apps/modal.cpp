// modal: the sequencer.

#include <modal/platform/paths.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/test_view.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace
{

using namespace modal;

void fail(const std::string& message)
{
    std::fprintf(stderr, "modal: %s\n", message.c_str());
    ui::sdl::showError("Modal", "Modal " + message + "."); // there's no console on Windows
}

// modal --write-config FILE [--force]: writes every setting, key binding and colour, each
// commented out at its default, to start a modalrc from. Returns the exit status.
int writeConfig(const std::filesystem::path& file, bool force)
{
    if (!force && std::filesystem::exists(file))
    {
        fail("won't overwrite " + file.string() + " without --force");
        return 1;
    }
    std::ofstream out{file, std::ios::binary};
    out << ui::defaultConfigText(ui::TestView::commands());
    if (!out.flush())
    {
        fail("can't write " + file.string());
        return 1;
    }
    std::printf("Wrote %s\n", file.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    std::optional<std::filesystem::path> configFile;
    bool force = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--write-config" && i + 1 < argc)
        {
            configFile = argv[++i];
        }
        else if (argument == "--force")
        {
            force = true;
        }
        else
        {
            fail("doesn't know " + std::string{argument} +
                 "; it takes --write-config FILE, with --force to overwrite FILE");
            return 2;
        }
    }
    if (configFile)
    {
        return writeConfig(*configFile, force);
    }

    // The built-in modalrc, then the user's. Errors go to stderr, where there is one, and
    // the first to the status line.
    ui::Config config;
    ui::ConfigReader reader{config, ui::TestView::commands(), platform::configDirectory()};
    ui::runStartupFiles(reader, platform::configDirectory());
    std::string message;
    for (const auto& error : reader.errors())
    {
        std::fprintf(stderr, "%s\n", error.text().c_str());
    }
    if (!reader.errors().empty())
    {
        const auto& first = reader.errors().front();
        message = ui::utf8Text(ui::utf8Path(first.source).filename()) + ":" +
                  std::to_string(first.line) + ": " + first.message;
        if (reader.errors().size() > 1)
        {
            message += " (and " + std::to_string(reader.errors().size() - 1) + " more)";
        }
    }
    const auto& settings = config.settings;
    const auto minimum = settings.cells("minsize");
    const auto points = static_cast<float>(settings.number("fontsize"));

    auto opened = ui::sdl::Window::open({.title = "Modal",
                                         .columns = 120,
                                         .rows = 36,
                                         .width = 0,
                                         .height = 0,
                                         .offscreen = false,
                                         .font = {.file = "", .points = points},
                                         .minimumColumns = minimum.columns,
                                         .minimumRows = minimum.rows});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        ui::sdl::showError("Modal", "Modal " + *error + ".");
        return 1;
    }
    auto& window = *std::get<std::unique_ptr<ui::sdl::Window>>(opened);
    // A font of the user's choosing, if it can be opened; the built-in one if not.
    if (const auto& font = settings.text("font"); !font.empty())
    {
        if (auto error = window.setFont({.file = font, .points = points}); !error.empty())
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            message = message.empty() ? error : message + "; " + error;
        }
    }

    // Until the event list (milestone 4), the test view.
    ui::TestView view{config.keymap, settings.integer("timeoutlen")};
    view.setMessage(message);
    const auto& theme = config.theme;
    window.run({.redraw =
                    [&](ui::sdl::Window& w)
                {
                    view.resize(w.pixelWidth(), w.pixelHeight(), w.cellSize());
                    w.draw(view.frame(), theme);
                },
                .key = [&](const ui::Key& key, std::int64_t now) { return view.handle(key, now); },
                .deadline = [&] { return view.deadline(); },
                .wake = [&](std::int64_t now) { return view.wake(now); }});
    return 0;
}
