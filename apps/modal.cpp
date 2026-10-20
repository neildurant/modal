// modal: the sequencer.

#include <modal/core/document.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/platform/paths.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/window_commands.hpp>

#include <algorithm>
#include <array>
#include <chrono>
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

// Applies the settings to the window, the view and the document, as at startup or after a
// change. A font that can't be opened is replaced by the built-in one, the font setting too
// so it isn't tried again, and that's the error returned.
std::string apply(ui::Settings& settings, ui::sdl::Window& window, ui::EventListView& view,
                  core::Document& document)
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
    view.setChordWindow(std::chrono::milliseconds{settings.integer("chordwindow")});
    document.setUndoLevels(static_cast<std::size_t>(settings.integer("undolevels")));
    return error;
}

// Opens a song file, or imports a MIDI file (.mid or .midi) as a new song, not to be saved
// over it. Returns what went wrong, or the first warning and how many more.
std::string open(core::Document& document, const std::filesystem::path& file)
{
    auto extension = ui::utf8Text(file.extension());
    std::ranges::transform(extension, extension.begin(), [](char c)
                           { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (extension == ".mid" || extension == ".midi")
    {
        auto result = core::importSmfFile(file);
        if (!result.song)
        {
            return "can't import " + ui::utf8Text(file) + ": " + result.error;
        }
        document.replace(std::move(*result.song), std::nullopt);
        if (result.warnings.empty())
        {
            return {};
        }
        return ui::utf8Text(file.filename()) + ": " + result.warnings.front() +
               (result.warnings.size() > 1
                    ? " (and " + std::to_string(result.warnings.size() - 1) + " more)"
                    : std::string{});
    }
    auto result = core::readSongFile(file);
    if (!result.song)
    {
        return result.error;
    }
    document.replace(std::move(*result.song), file);
    return {};
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
    std::optional<std::filesystem::path> songFile;
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
        else if (!argument.starts_with("--") && !songFile)
        {
            songFile = ui::utf8Path(argument);
        }
        else
        {
            fail("doesn't know " + std::string{argument} +
                 "; it takes a song or MIDI file to open, or --write-config FILE, with --force "
                 "to overwrite FILE");
            return 2;
        }
    }

    // The built-in modalrc, then the user's. Errors go to stderr, where there is one, and
    // the first to the status line.
    // Every command, described before the config binds keys to them; the view and the
    // window give them what runs them once they exist.
    ui::CommandRegistry commands;
    commands.add(ui::EventListView::commands());
    commands.add(ui::windowCommands());
    commands.add(ui::configCommands());
    commands.add(ui::promptCommands());
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

    // The song: the file given, or else the demo song from the arrangement sketch, until
    // there are file commands.
    core::Document document{ui::demoSong()};
    if (songFile)
    {
        if (auto error = open(document, *songFile); !error.empty())
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            message = message.empty() ? error : message + "; " + error;
        }
    }

    ui::EventListView view{commands, document, config.keymap};
    if (auto error = apply(settings, window, view, document); !error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        message = message.empty() ? error : message + "; " + error;
    }
    if (!message.empty())
    {
        view.setMessage(message, ui::EventListView::MessageKind::Warning);
    }

    // The window's commands (see windowCommands): the text size, for now.
    const auto fontSize = [&](std::string_view command, const ui::CommandCall& call)
    {
        const auto* setting = settings.find("fontsize");
        double size = settings.number("fontsize");
        const int steps = std::max(call.count, 1);
        size = command == "font-bigger"    ? size + steps
               : command == "font-smaller" ? size - steps
                                           : configuredSize;
        size = std::clamp(size, setting->minimum, setting->maximum);
        settings.set("fontsize", ui::formatValue(size));
        if (auto error = apply(settings, window, view, document); !error.empty())
        {
            return ui::CommandResult::error(std::move(error));
        }
        const auto cell = window.cellSize();
        return ui::CommandResult{true,
                                 "font " + ui::formatValue(size) + " pt, cells " +
                                     std::to_string(cell.width) + "×" +
                                     std::to_string(cell.height) + " px",
                                 false};
    };
    for (const auto& info : ui::windowCommands())
    {
        commands.add(info, [&, name = info.name](const ui::CommandCall& call)
                     { return fontSize(name, call); });
    }

    // Lines run at the : prompt, through the registry, after which settings and bindings
    // apply at once.
    ui::addPromptCommands(commands, reader, config, configDirectory, [&] { window.close(); });
    view.setLineHandler(
        [&](const std::string& line)
        {
            const auto result = commands.runLine(line);
            view.setKeymap(config.keymap);
            auto message = result.message;
            bool warning = result.warning;
            if (auto error = apply(settings, window, view, document); !error.empty())
            {
                message = error;
                warning = true;
            }
            if (!message.empty())
            {
                view.setMessage(message, warning ? ui::EventListView::MessageKind::Warning
                                                 : ui::EventListView::MessageKind::Information);
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
