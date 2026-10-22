// SPDX-License-Identifier: GPL-3.0-or-later

// modal: the sequencer.

#include <modal/core/document.hpp>
#include <modal/core/engine.hpp>
#include <modal/core/player.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/midi_output.hpp>
#include <modal/platform/midi_ports.hpp>
#include <modal/platform/paths.hpp>
#include <modal/platform/thread_priority.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/file_commands.hpp>
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
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace
{

using namespace modal;

// Playback: the engine on a thread of its own at realtime priority, sending to the MIDI
// outputs, and the player, which opens the ports the tracks name as they're needed. When it
// goes, playback stops and the thread ends.
class Playback
{
public:
    explicit Playback(const ui::Settings& settings)
        : settings_(settings)
    {
        std::promise<std::string> priority;
        auto error = priority.get_future();
        thread_ = std::thread{[this, &priority]
                              {
                                  const platform::RealtimePriority realtime;
                                  priority.set_value(realtime.error());
                                  engine_.run();
                              }};
        priorityError_ = error.get();
    }

    ~Playback()
    {
        engine_.stop();
        while (!engine_.quit())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        thread_.join();
    }

    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;

    core::Player& player() { return player_; }

    // Why the engine runs at normal priority, or nothing.
    const std::string& priorityError() const { return priorityError_; }

    // Looks the ports up again at the next update, for a new song.
    void songReplaced() { lookedUp_ = false; }

    // Hands the player the song, looking the ports up the first time, for a new song, and
    // whenever the port setting has changed. Then returns which ports weren't found, if any.
    std::string update(const core::Song& song)
    {
        const bool lookUp = !lookedUp_ || settings_.text("port") != port_;
        if (lookUp)
        {
            port_ = settings_.text("port");
            lookedUp_ = true;
            player_.forgetPorts();
        }
        player_.update(song);
        if (!lookUp)
        {
            return {};
        }
        std::string missing;
        for (const auto& name : player_.missingPorts())
        {
            missing += (missing.empty() ? "" : ", ") +
                       (!name.empty()   ? name
                        : port_.empty() ? std::string{"any output, for tracks without a port"}
                                        : port_ + ", for tracks without a port");
        }
        return missing.empty() ? missing : "Ports not found, so not played: " + missing;
    }

private:
    // A track's port, or for one without a port, the port setting's or the first found.
    std::optional<core::DestinationId> open(std::string_view name)
    {
        std::string wanted{name.empty() ? std::string_view{port_} : name};
        if (wanted.empty())
        {
            const auto ports = platform::listMidiPorts().outputs;
            if (ports.empty())
            {
                return std::nullopt;
            }
            wanted = ports.front().name;
        }
        const auto opened = outputs_.open(wanted);
        return opened.ok() ? std::optional{opened.destination} : std::nullopt;
    }

    const ui::Settings& settings_;
    std::string port_; // the port setting when ports were last looked up
    bool lookedUp_ = false;
    platform::MidiOutputs outputs_;
    platform::SystemClock clock_;
    core::Engine engine_{outputs_, clock_};
    core::Player player_{engine_, [this](std::string_view name) { return open(name); }};
    std::thread thread_;
    std::string priorityError_;
};

// The file commands' way to the window: its dialogs, closing it, and the view.
class FileHost final : public ui::FileHost
{
public:
    FileHost(ui::sdl::Window& window, ui::EventListView& view, core::Document& document,
             Playback& playback)
        : window_(window)
        , view_(view)
        , document_(document)
        , playback_(playback)
    {
    }

    void chooseOpen(Chosen done) override
    {
        const auto& file = document_.file() ? document_.file() : document_.importedFrom();
        window_.chooseFile(false,
                           {{"Songs and MIDI files", "modal;mid;midi"},
                            {"Songs", "modal"},
                            {"MIDI files", "mid;midi"}},
                           file ? file->parent_path() : std::filesystem::path{}, std::move(done));
    }

    void chooseSave(FileKind kind, const std::filesystem::path& suggested, Chosen done) override
    {
        window_.chooseFile(true,
                           {kind == FileKind::Song ? ui::sdl::FileFilter{"Songs", "modal"}
                                                   : ui::sdl::FileFilter{"MIDI files", "mid"}},
                           suggested, std::move(done));
    }

    void quit() override { window_.close(); }

    void songReplaced() override
    {
        view_.showSong();
        playback_.songReplaced();
        if (auto missing = playback_.update(document_.song()); !missing.empty())
        {
            missing_ = std::move(missing);
        }
    }

    void report(const ui::CommandResult& result) override
    {
        if (!result.message.empty())
        {
            view_.setMessage(result.message, result.warning
                                                 ? ui::EventListView::MessageKind::Warning
                                                 : ui::EventListView::MessageKind::Information);
        }
    }

    // Which ports a new song names that weren't found, once, to show after what opened it.
    std::string takeMissingPorts() { return std::exchange(missing_, {}); }

private:
    ui::sdl::Window& window_;
    ui::EventListView& view_;
    core::Document& document_;
    Playback& playback_;
    std::string missing_;
};

// Applies the settings to the window, the view and the document, as at startup or after a
// change. A font that can't be opened is replaced by the built-in one, the font setting too
// so it isn't tried again, and that's the error returned.
std::string apply(ui::Settings& settings, ui::sdl::Window& window, ui::EventListView& view,
                  core::Document& document, Playback& playback)
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
    view.setRewind(settings.flag("rewind"));
    if (auto missing = playback.update(document.song()); error.empty())
    {
        error = std::move(missing);
    }
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
    std::optional<std::filesystem::path> songFile;
    bool force = false;
    bool demo = false;
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
        else if (argument == "--demo")
        {
            demo = true;
        }
        else if (!argument.starts_with("--") && !songFile)
        {
            songFile = ui::utf8Path(argument);
        }
        else
        {
            fail("doesn't know " + std::string{argument} +
                 "; it takes a song or MIDI file to open, or --demo for the demo song, or "
                 "--write-config FILE, with --force to overwrite FILE");
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
    commands.add(ui::fileCommands());
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

    // Playing, through the engine; each change to the song is handed to it.
    Playback playback{settings};
    if (!playback.priorityError().empty())
    {
        const auto warning =
            "Playback runs at normal priority, so timing may suffer: " + playback.priorityError();
        std::fprintf(stderr, "%s\n", warning.c_str());
        message = message.empty() ? warning : message + "; " + warning;
    }

    // The song: the file given, or a new song, or with --demo the demo song from the
    // arrangement sketch.
    core::Document document{demo ? ui::demoSong() : ui::newSong()};
    document.setListener([&](const core::Song& song) { playback.player().update(song); });
    if (songFile)
    {
        if (auto loaded = ui::openFile(document, *songFile); !loaded.message.empty())
        {
            std::fprintf(stderr, "%s\n", loaded.message.c_str());
            message = message.empty() ? loaded.message : message + "; " + loaded.message;
        }
    }

    ui::EventListView view{commands, document, config.keymap};
    view.setPlayer(&playback.player());
    view.setPortFinder(
        [](std::string_view name)
        {
            const auto outputs = platform::listMidiPorts().outputs;
            ui::EventListView::PortLookup lookup;
            for (const auto& port : outputs)
            {
                lookup.ports.push_back(port.name);
            }
            if (!name.empty())
            {
                const auto match = platform::matchPort(outputs, name);
                if (match.index)
                {
                    lookup.port = outputs[*match.index].name;
                }
                for (const auto i : match.candidates)
                {
                    lookup.candidates.push_back(outputs[i].name);
                }
            }
            return lookup;
        });
    if (auto error = apply(settings, window, view, document, playback); !error.empty())
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
        if (auto error = apply(settings, window, view, document, playback); !error.empty())
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
    ui::addPromptCommands(commands, reader, config, configDirectory);
    FileHost files{window, view, document, playback};
    ui::addFileCommands(commands, document, files);
    view.setLineHandler(
        [&](const std::string& line)
        {
            const auto result = commands.runLine(line);
            view.setKeymap(config.keymap);
            auto message = result.message;
            bool warning = result.warning;
            if (auto error = apply(settings, window, view, document, playback); !error.empty())
            {
                message = error;
                warning = true;
            }
            if (auto missing = files.takeMissingPorts(); !missing.empty())
            {
                message = message.empty() ? missing : message + "; " + missing;
                warning = true;
            }
            if (!message.empty())
            {
                view.setMessage(message, warning ? ui::EventListView::MessageKind::Warning
                                                 : ui::EventListView::MessageKind::Information);
            }
        });

    // Closing with unsaved changes asks first (decided in milestone 4). Saving goes through
    // :wq, which closes the window once it has saved, after the save dialog if it needs one.
    const auto closing = [&]
    {
        if (!document.modified())
        {
            return true;
        }
        const std::array<std::string, 3> buttons{"Save", "Don't save", "Cancel"};
        const auto name = document.file()           ? ui::utf8Text(document.file()->filename())
                          : document.importedFrom() ? std::string{"The imported song"}
                                                    : std::string{"The new song"};
        const auto choice = window.ask(
            "Modal", name + " has changes not saved. Save them before closing?", buttons);
        if (choice == 0)
        {
            files.report(commands.runLine("wq"));
        }
        return choice == 1;
    };

    // The window's title names the song, as the status line does.
    const auto title = [&]
    {
        const auto& file = document.file();
        const auto& imported = document.importedFrom();
        const auto name = file       ? ui::utf8Text(file->filename())
                          : imported ? ui::utf8Text(imported->filename()) + " (imported)"
                                     : std::string{"Untitled"};
        return name + (document.modified() ? " [+]" : "") + " – Modal";
    };

    const auto& theme = config.theme;
    window.run({.redraw =
                    [&](ui::sdl::Window& w)
                {
                    view.resize(w.pixelWidth(), w.pixelHeight(), w.cellSize());
                    w.setTitle(title());
                    w.draw(view.frame(), theme);
                },
                .key = [&](const ui::Key& key, std::int64_t now) { return view.handle(key, now); },
                .deadline = [&] { return view.deadline(); },
                .wake = [&](std::int64_t now) { return view.wake(now); },
                .closing = closing});
    return 0;
}
