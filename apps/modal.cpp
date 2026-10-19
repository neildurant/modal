// modal: the sequencer.

#include <modal/platform/paths.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/test_view.hpp>
#include <modal/ui/window_commands.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{

using namespace modal;

// The test view's commands and the window's.
std::vector<ui::CommandInfo> allCommands()
{
    std::vector<ui::CommandInfo> commands{ui::TestView::commands().begin(),
                                          ui::TestView::commands().end()};
    commands.insert(commands.end(), ui::windowCommands().begin(), ui::windowCommands().end());
    return commands;
}

// Applies the settings to the window and the view, as at startup or after a change. A font
// that can't be opened is replaced by the built-in one, the font setting too so it isn't
// tried again, and that's the error returned.
std::string apply(ui::Settings& settings, ui::sdl::Window& window, ui::TestView& view)
{
    std::string error;
    const ui::sdl::FontOptions font{.file = settings.text("font"),
                                    .points = static_cast<float>(settings.number("fontsize"))};
    if (font != window.font())
    {
        error = window.setFont(font);
        if (!error.empty() && !font.file.empty())
        {
            settings.set("font", "");
            window.setFont({.file = "", .points = font.points});
        }
    }
    const auto minimum = settings.cells("minsize");
    window.setMinimumCells(minimum.columns, minimum.rows);
    view.setTimeout(settings.integer("timeoutlen"));
    return error;
}

void fail(const std::string& message)
{
    std::fprintf(stderr, "modal: %s\n", message.c_str());
    ui::sdl::showError("Modal", "Modal " + message + "."); // there's no console on Windows
}

// modal --write-config FILE [--force]: writes the config, after modalrc, to start a modalrc
// from (see configText). Returns the exit status.
int writeConfig(const std::filesystem::path& file, bool force, const std::string& text)
{
    if (auto error = ui::writeConfigFile(file, text, force); !error.empty())
    {
        fail(error + (force ? "" : "; add --force to overwrite it"));
        return 1;
    }
    std::printf("Wrote %s\n", ui::utf8Text(file).c_str());
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

    // The built-in modalrc, then the user's. Errors go to stderr, where there is one, and
    // the first to the status line.
    const auto commands = allCommands();
    ui::Config config;
    const auto configDirectory = platform::configDirectory();
    ui::ConfigReader reader{config, commands, configDirectory};
    ui::runStartupFiles(reader, configDirectory);
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
    if (configFile)
    {
        return writeConfig(*configFile, force, ui::configText(config, commands, configDirectory));
    }
    auto& settings = config.settings;
    const double configuredSize = settings.number("fontsize");

    // Opened in the built-in font, which a user's font then replaces if it can be opened.
    auto opened =
        ui::sdl::Window::open({.title = "Modal",
                               .columns = 120,
                               .rows = 36,
                               .width = 0,
                               .height = 0,
                               .offscreen = false,
                               .font = {.file = "", .points = static_cast<float>(configuredSize)},
                               .minimumColumns = 0,
                               .minimumRows = 0});
    if (const auto* error = std::get_if<std::string>(&opened))
    {
        ui::sdl::showError("Modal", "Modal " + *error + ".");
        return 1;
    }
    auto& window = *std::get<std::unique_ptr<ui::sdl::Window>>(opened);

    // Until the event list (milestone 4), the test view.
    ui::TestView view{config.keymap};
    if (auto error = apply(settings, window, view); !error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        message = message.empty() ? error : message + "; " + error;
    }
    if (!message.empty())
    {
        view.setMessage(message, ui::TestView::MessageKind::Warning);
    }

    // The window's commands (see windowCommands): the text size, for now.
    view.setCommandHandler(
        [&](std::string_view command, int count) -> std::optional<bool>
        {
            const auto* setting = settings.find("fontsize");
            double size = settings.number("fontsize");
            const int steps = std::max(count, 1);
            if (command == "font-bigger")
            {
                size += steps;
            }
            else if (command == "font-smaller")
            {
                size -= steps;
            }
            else if (command == "font-reset")
            {
                size = configuredSize;
            }
            else
            {
                return std::nullopt;
            }
            size = std::clamp(size, setting->minimum, setting->maximum);
            settings.set("fontsize", ui::formatValue(size));
            if (auto error = apply(settings, window, view); !error.empty())
            {
                view.setMessage(error, ui::TestView::MessageKind::Warning);
                return true;
            }
            const auto cell = window.cellSize();
            view.setMessage("font " + ui::formatValue(size) + " pt, cells " +
                                std::to_string(cell.width) + "×" + std::to_string(cell.height) +
                                " px",
                            ui::TestView::MessageKind::Information);
            return true;
        });

    // Lines run at the : prompt, after which settings and bindings apply at once.
    view.setLineHandler(
        [&](const std::string& line)
        {
            const auto result = ui::runPromptLine(reader, config, line, configDirectory, commands);
            if (result.quit)
            {
                window.close();
                return;
            }
            view.setKeymap(config.keymap);
            auto message = result.message;
            bool warning = result.warning;
            if (auto error = apply(settings, window, view); !error.empty())
            {
                message = error;
                warning = true;
            }
            if (!message.empty())
            {
                view.setMessage(message, warning ? ui::TestView::MessageKind::Warning
                                                 : ui::TestView::MessageKind::Information);
            }
        });

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
