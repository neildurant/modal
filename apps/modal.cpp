// SPDX-License-Identifier: GPL-3.0-or-later

// modal: the sequencer.

#include <modal/core/document.hpp>
#include <modal/core/engine.hpp>
#include <modal/core/player.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song_file.hpp>
#include <modal/core/version.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/midi_input_session.hpp>
#include <modal/platform/midi_output.hpp>
#include <modal/platform/midi_ports.hpp>
#include <modal/platform/paths.hpp>
#include <modal/platform/thread_priority.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/ports.hpp>
#include <modal/ui/prompt.hpp>
#include <modal/ui/sdl/window.hpp>
#include <modal/ui/themes.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{

// Where a crash is written down, if there's a config directory.
std::optional<std::filesystem::path> crashLog;

std::string twoDigits(unsigned value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

// The time now, in UTC: 2026-10-23 09:41:07.
std::string utcNow()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss time{now - day};
    return std::to_string(static_cast<int>(date.year())) + "-" +
           twoDigits(static_cast<unsigned>(date.month())) + "-" +
           twoDigits(static_cast<unsigned>(date.day())) + " " +
           twoDigits(static_cast<unsigned>(time.hours().count())) + ":" +
           twoDigits(static_cast<unsigned>(time.minutes().count())) + ":" +
           twoDigits(static_cast<unsigned>(time.seconds().count()));
}

// What std::terminate runs, as for an exception nothing caught: says what it was, on stderr
// and at the end of crash.log in the config directory, so a crash leaves a clue, then aborts
// as it would have.
[[noreturn]] void logCrash() noexcept
{
    std::string what = "terminate called with no exception";
    if (const auto exception = std::current_exception())
    {
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& e)
        {
            what = std::string{"uncaught exception: "} + e.what();
        }
        catch (...)
        {
            what = "uncaught exception of an unknown type";
        }
    }
    const auto line = utcNow() + " UTC  modal " + std::string{modal::core::version()} + ": " + what;
    std::fprintf(stderr, "%s\n", line.c_str());
    if (crashLog)
    {
        std::error_code error;
        std::filesystem::create_directories(crashLog->parent_path(), error);
        std::ofstream file{*crashLog, std::ios::app};
        file << line << "\n";
    }
    std::abort();
}

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
        inputs_.reset(); // close callbacks before stopping the engine they send to
        while (!engine_.stop())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
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
    // whenever the ports have changed. Then says which tracks' ports can't be used, and why.
    std::string update(const core::Song& song)
    {
        const bool lookUp = !lookedUp_;
        if (lookUp)
        {
            lookedUp_ = true;
            player_.forgetPorts();
        }
        const auto oldInputs = player_.missingInputs();
        player_.update(song);
        std::string warning;
        const auto add = [&](std::string text)
        {
            if (!text.empty())
                warning += (warning.empty() ? "" : "; ") + text;
        };
        if (lookUp)
        {
            add(unusable(ui::PortDirection::Output, player_.missingPorts()));
        }
        if (lookUp || oldInputs != player_.missingInputs())
        {
            add(unusable(ui::PortDirection::Input, player_.missingInputs()));
        }
        return warning;
    }

    // The ports of this machine, as the Ports view sets them, and where to keep them when they
    // change. Looks for the ports there are now.
    void setPorts(ui::PortTable ports, std::function<void(const ui::PortTable&)> save)
    {
        ports_ = std::move(ports);
        savePorts_ = std::move(save);
        detectPorts();
    }
    const ui::PortTable& ports() const { return ports_; }

    // Puts a changed table into use, from the Ports view: keeps it, listens to the inputs it
    // enables, and has the tracks' ports looked up again.
    void changePorts(const ui::PortTable& table)
    {
        ports_ = table;
        if (savePorts_)
            savePorts_(ports_);
        lookedUp_ = false;
    }

    // Notes the ports the system lists now, adding new ones, disabled, and keeping the file.
    void detectPorts()
    {
        const auto system = platform::listMidiPorts();
        std::vector<std::string> inputs;
        for (const auto& port : system.inputs)
            inputs.push_back(port.name);
        std::vector<std::string> outputs;
        for (const auto& port : allPorts(system.outputs))
            outputs.push_back(port.name);
        const bool addedIn = ports_.detected(ui::PortDirection::Input, inputs);
        const bool addedOut = ports_.detected(ui::PortDirection::Output, outputs);
        if ((addedIn || addedOut) && savePorts_)
            savePorts_(ports_);
        lookedUp_ = false;
    }

    // Modal's own ports, then the system's output ports.
    std::vector<platform::MidiPort> allPorts(
        const std::vector<platform::MidiPort>& system = platform::listMidiPorts().outputs) const
    {
        std::vector<platform::MidiPort> ports;
        for (const auto& own : own_)
        {
            ports.push_back({own.first});
        }
        ports.insert(ports.end(), system.begin(), system.end());
        return ports;
    }

    // Makes the ports of its own the settings ask for that aren't made yet, and has the tracks'
    // ports looked up again, since a track may name one. Returns what went wrong, if anything,
    // or which ports no longer wanted stay until Modal restarts.
    std::string openOwnPorts()
    {
        const auto wanted = ui::ownPortNames(settings_);
        if (wanted == ownWanted_)
        {
            return {};
        }
        ownWanted_ = wanted;
        lookedUp_ = false;
        if (!wanted.empty() && !platform::canCreateOwnPorts())
        {
            // The default asks for one; only someone who asked for ports of their own is told.
            const auto* setting = settings_.find("ownports");
            if (setting->value == setting->defaultValue)
            {
                return {};
            }
        }
        std::string problem;
        for (const auto& name : wanted)
        {
            if (std::ranges::find(
                    own_, name, &std::pair<std::string, core::DestinationId>::first) != own_.end())
            {
                continue;
            }
            auto made = outputs_.openOwn(name);
            if (!made.ok())
            {
                problem = std::move(made.error);
                break;
            }
            own_.emplace_back(name, made.destination);
        }
        detectPorts(); // Modal's own are outputs like any other, enabled in the Ports view
        if (!problem.empty())
        {
            problem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(problem[0])));
            return problem;
        }
        std::string staying;
        for (const auto& own : own_)
        {
            if (std::ranges::find(wanted, own.first) == wanted.end())
            {
                staying += (staying.empty() ? "" : ", ") + own.first;
            }
        }
        return staying.empty() ? staying : staying + " stay open until Modal restarts";
    }

    // Input ownership, ID mapping and reset acknowledgements share one testable session.
    void setInputWake(std::function<void()> wake) { inputs_->setWake(std::move(wake)); }
    // Listens to the enabled inputs there are.
    std::string openInputs()
    {
        return inputs_->setInputs(ports_.usable(ui::PortDirection::Input), ui::sdl::Window::now());
    }
    std::optional<std::int64_t> inputDeadline() const { return inputs_->deadline(); }
    std::string pollInputs(std::int64_t now) { return inputs_->poll(now); }

private:
    // A track's port, or for one without a port, the default output: an enabled port there now,
    // by alias or detected name, or none.
    std::optional<core::DestinationId> open(std::string_view name)
    {
        const auto resolved = ports_.resolve(ui::PortDirection::Output, name);
        if (!resolved.port)
        {
            return std::nullopt;
        }
        const auto own = std::ranges::find(own_, *resolved.port,
                                           &std::pair<std::string, core::DestinationId>::first);
        if (own != own_.end())
        {
            return own->second;
        }
        const auto opened = outputs_.open(*resolved.port);
        return opened.ok() ? std::optional{opened.destination} : std::nullopt;
    }

    // Which of the tracks' ports can't be used, by why: not found, or disabled.
    std::string unusable(ui::PortDirection direction, const std::vector<std::string>& names) const
    {
        std::string missing;
        std::string disabled;
        for (const auto& name : names)
        {
            const auto resolved = ports_.resolve(direction, name);
            const auto shown =
                !name.empty() ? name
                : direction == ui::PortDirection::Input
                    ? std::string{"no default input is set, for tracks set to Default"}
                    : std::string{"no default output is set, for tracks set to Default"};
            auto& list = resolved.problem == ui::PortProblem::Disabled ? disabled : missing;
            list += (list.empty() ? "" : ", ") + shown;
        }
        const bool input = direction == ui::PortDirection::Input;
        std::string text;
        if (!missing.empty())
            text = (input ? "Inputs not found: " : "Ports not found, so not played: ") + missing;
        if (!disabled.empty())
            text += (text.empty() ? "" : "; ") +
                    std::string{input ? "Inputs disabled: " : "Ports disabled, so not played: "} +
                    disabled + " (enable them in the Ports view)";
        return text;
    }

    const ui::Settings& settings_;
    ui::PortTable ports_;
    std::function<void(const ui::PortTable&)> savePorts_;
    bool lookedUp_ = false;
    std::vector<std::pair<std::string, core::DestinationId>> own_; // Modal's own ports
    std::vector<std::string> ownWanted_;                           // as the settings last asked
    platform::MidiOutputs outputs_;
    platform::SystemClock clock_;
    core::Engine engine_{outputs_, clock_};
    core::Player player_{engine_, [this](std::string_view name) { return open(name); },
                         [this](std::string_view name) -> std::optional<core::InputId>
                         {
                             const auto resolved = ports_.resolve(ui::PortDirection::Input, name);
                             return resolved.port ? inputs_->inputId(*resolved.port) : std::nullopt;
                         }};
    std::thread thread_;
    std::string priorityError_;
    // Reset explicitly before engine shutdown; callbacks can still arrive until it closes.
    std::unique_ptr<platform::MidiInputSession> inputs_ =
        std::make_unique<platform::MidiInputSession>(engine_);
};

// The file commands' way to the window: its dialogs, closing it, and the view.
class FileHost final : public ui::FileHost
{
public:
    FileHost(ui::sdl::Window& window, ui::Workspace& view, core::Document& document,
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
                           {kind == FileKind::Song   ? ui::sdl::FileFilter{"Songs", "modal"}
                            : kind == FileKind::Midi ? ui::sdl::FileFilter{"MIDI files", "mid"}
                                                     : ui::sdl::FileFilter{"All files", "*"}},
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

    void report(const ui::CommandResult& result) override { view_.report(result); }

    // Which ports a new song names that weren't found, once, to show after what opened it.
    std::string takeMissingPorts() { return std::exchange(missing_, {}); }

private:
    ui::sdl::Window& window_;
    ui::Workspace& view_;
    core::Document& document_;
    Playback& playback_;
    std::string missing_;
};

// Applies the settings to the window, the view and the document, as at startup or after a
// change. A font that can't be opened is replaced by the built-in one, the font setting too
// so it isn't tried again, and that's the error returned.
std::string apply(ui::Settings& settings, ui::sdl::Window& window, ui::Workspace& view,
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
    view.setHints(settings.flag("hints"), settings.integer("hintdelay"));
    {
        // One key, which <leader> in the bindings stands for.
        const auto leader = ui::parseKeys(settings.text("leader"));
        const auto* keys = std::get_if<std::vector<ui::Key>>(&leader);
        if (keys != nullptr && keys->size() == 1 && keys->front().named != ui::NamedKey::Leader)
        {
            view.setLeader(keys->front());
        }
        else if (error.empty())
        {
            error = "leader is one key, such as , or <Space>";
        }
    }
    view.eventList().setChordWindow(std::chrono::milliseconds{settings.integer("chordwindow")});
    view.pianoRoll().setChordWindow(std::chrono::milliseconds{settings.integer("chordwindow")});
    view.setChordWindow(std::chrono::milliseconds{settings.integer("chordwindow")});
    view.setPedalTiming(std::chrono::milliseconds{settings.integer("pedallift")},
                        std::chrono::milliseconds{settings.integer("pedaldown")});
    view.pianoRoll().setNudge(std::chrono::milliseconds{settings.integer("nudge")});
    view.pianoRoll().setGridAdvance(settings.flag("gridadvance"));
    view.setHumanize(std::chrono::milliseconds{settings.integer("humanizetime")},
                     static_cast<int>(settings.integer("humanizevel")));
    const auto clip = settings.text("velclip");
    if (clip != "soft" && clip != "hard" && error.empty())
    {
        error = "velclip is soft or hard";
    }
    view.setVelocityLimits(
        {.soft = clip != "hard", .floor = static_cast<int>(settings.integer("velfloor"))});
    const auto pedal = settings.text("quantizepedal");
    if (pedal != "follow" && pedal != "keep" && pedal != "ask" && error.empty())
    {
        error = "quantizepedal is follow, keep or ask";
    }
    view.setQuantizePedal(pedal == "keep"  ? ui::Workspace::QuantizePedal::Keep
                          : pedal == "ask" ? ui::Workspace::QuantizePedal::Ask
                                           : ui::Workspace::QuantizePedal::Follow);
    document.setUndoLevels(static_cast<std::size_t>(settings.integer("undolevels")));
    view.setRewind(settings.flag("rewind"));
    view.setAudition(settings.flag("audition"));
    view.setThru(settings.flag("thru"));
    view.setQuantizeMarks(settings.flag("quantizemarks"));
    if (auto own = playback.openOwnPorts(); error.empty())
    {
        error = std::move(own);
    }
    if (auto inputs = playback.openInputs(); error.empty())
    {
        error = std::move(inputs);
    }
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
    commands.add(ui::Workspace::commands());
    commands.add(ui::windowCommands());
    commands.add(ui::configCommands());
    commands.add(ui::promptCommands());
    commands.add(ui::fileCommands());
    ui::Config config;
    const auto configDirectory = platform::configDirectory();
    if (const auto stateDirectory = platform::stateDirectory())
    {
        crashLog = *stateDirectory / "crash.log"; // what Modal writes, not the user
    }
    std::set_terminate(logCrash);
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
    playback.setInputWake(window.waker()); // the window outlives playback
    if (!playback.priorityError().empty())
    {
        const auto warning =
            "Playback runs at normal priority, so timing may suffer: " + playback.priorityError();
        std::fprintf(stderr, "%s\n", warning.c_str());
        message = message.empty() ? warning : message + "; " + warning;
    }

    // The machine's MIDI ports, as the Ports view sets them, kept in the state directory and
    // written back after each change: a file that may be missing, or deleted, at any time.
    {
        ui::PortTable ports;
        std::function<void(const ui::PortTable&)> save;
        if (const auto stateDirectory = platform::stateDirectory())
        {
            const auto file = *stateDirectory / "ports";
            std::ifstream in{file, std::ios::binary};
            const std::string text{std::istreambuf_iterator<char>{in},
                                   std::istreambuf_iterator<char>{}};
            std::vector<std::string> errors;
            ports = ui::PortTable::read(text, &errors);
            for (const auto& error : errors)
            {
                std::fprintf(stderr, "%s\n", error.c_str());
            }
            if (!errors.empty())
            {
                message = message.empty() ? errors.front() : message + "; " + errors.front();
            }
            save = [file](const ui::PortTable& table)
            {
                std::error_code error;
                std::filesystem::create_directories(file.parent_path(), error);
                std::ofstream out{file, std::ios::binary | std::ios::trunc};
                out << table.text();
            };
        }
        playback.setPorts(std::move(ports), std::move(save));
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

    ui::Workspace view{commands, document, config.keymap};
    view.setScales(config.scales);
    view.setPlayer(&playback.player());
    // What's remembered between sessions, read from the state directory and written back there
    // after each change: a file that may be missing, or deleted, at any time.
    if (const auto stateDirectory = platform::stateDirectory())
    {
        const auto file = *stateDirectory / "remembered";
        std::ifstream in{file, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        view.setRemembered(ui::Remembered::read(text),
                           [file](const ui::Remembered& remembered)
                           {
                               std::error_code error;
                               std::filesystem::create_directories(file.parent_path(), error);
                               std::ofstream out{file, std::ios::binary | std::ios::trunc};
                               out << remembered.text();
                           });
    }
    view.show(ui::ViewKind::Arrangement); // the home view
    // The Ports view's table, and what changing it or looking again does: listen to the
    // enabled inputs, look the tracks' ports up again, and say what can't be used.
    const auto portsChanged = [&]
    {
        auto problem = playback.openInputs();
        auto missing = playback.update(document.song());
        if (problem.empty())
            problem = std::move(missing);
        if (!problem.empty())
            view.setMessage(std::move(problem), ui::MessageKind::Warning);
    };
    view.setPortControl({.table = [&] { return &playback.ports(); },
                         .change =
                             [&](const ui::PortTable& table)
                         {
                             playback.changePorts(table);
                             portsChanged();
                         },
                         .look =
                             [&]
                         {
                             playback.detectPorts();
                             portsChanged();
                         }});
    // Ports plugged in or out while Modal runs: the watcher's thread wakes the window, which
    // looks again.
    std::atomic<bool> portsMoved{false};
    const auto wakeWindow = window.waker();
    const platform::PortWatcher portWatcher{[&portsMoved, wakeWindow]
                                            {
                                                portsMoved.store(true);
                                                wakeWindow();
                                            }};

    // The Tracks choosers and :port offer the enabled ports, by alias where they have one.
    view.setInputPorts([&] { return playback.ports().offered(ui::PortDirection::Input); });
    view.setPortFinder(
        [&](std::string_view name)
        {
            ui::Workspace::PortLookup lookup;
            lookup.ports = playback.ports().offered(ui::PortDirection::Output);
            if (!name.empty())
            {
                std::vector<platform::MidiPort> offered;
                for (const auto& port : lookup.ports)
                {
                    offered.push_back({port});
                }
                const auto match = platform::matchPort(offered, name);
                if (match.index)
                {
                    lookup.port = lookup.ports[*match.index];
                }
                for (const auto i : match.candidates)
                {
                    lookup.candidates.push_back(lookup.ports[i]);
                }
            }
            return lookup;
        });
    view.setSettings(&settings);
    // The choices completion offers for settings and files; the workspace has the rest.
    view.setChoices(ui::Choices::Setting,
                    [&](std::string_view word)
                    {
                        std::vector<std::string> themes;
                        for (const auto name : ui::builtInThemeNames())
                        {
                            themes.emplace_back(name);
                        }
                        if (configDirectory)
                        {
                            for (auto& file : ui::fileChoices(*configDirectory / "themes", ""))
                            {
                                if (!file.ends_with('/'))
                                {
                                    themes.push_back(std::move(file));
                                }
                            }
                        }
                        return ui::settingChoices(settings, word, themes);
                    });
    const auto home = platform::homeDirectory();
    view.setFolders(configDirectory, home);
    if (auto error = apply(settings, window, view, document, playback); !error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        message = message.empty() ? error : message + "; " + error;
    }
    // Every port starts disabled, so say why nothing plays or listens until some are enabled.
    if (playback.ports().offered(ui::PortDirection::Input).empty() &&
        playback.ports().offered(ui::PortDirection::Output).empty())
    {
        const std::string none = "No MIDI ports are enabled: choose them in the Ports view (, v m)";
        message = message.empty() ? none : none + "; " + message;
    }
    if (!message.empty())
    {
        view.setMessage(message, ui::MessageKind::Warning);
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
    FileHost files{window, view, document, playback};
    ui::addPromptCommands(commands, reader, config, configDirectory, &files, home);
    ui::addFileCommands(commands, document, files, home);
    view.setLineHandler(
        [&](const std::string& line)
        {
            auto result = commands.runLine(line);
            view.setKeymap(config.keymap);
            view.setScales(config.scales);
            if (auto error = apply(settings, window, view, document, playback); !error.empty())
            {
                result.message = error;
                result.warning = true;
                result.refused = true;
            }
            if (auto missing = files.takeMissingPorts(); !missing.empty())
            {
                result.message = result.message.empty() ? missing : result.message + "; " + missing;
                result.warning = true;
            }
            view.report(result);
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
                .deadline =
                    [&]
                {
                    const auto viewDue = view.deadline();
                    const auto inputDue = playback.inputDeadline();
                    return viewDue && inputDue ? std::optional{std::min(*viewDue, *inputDue)}
                           : viewDue           ? viewDue
                                               : inputDue;
                },
                .wake =
                    [&](std::int64_t now)
                {
                    bool changed = view.wake(now);
                    if (portsMoved.exchange(false))
                    {
                        playback.detectPorts();
                        portsChanged();
                        view.refresh();
                        changed = true;
                    }
                    if (const auto due = playback.inputDeadline(); due && now >= *due)
                    {
                        auto error = playback.pollInputs(now);
                        if (!playback.inputDeadline())
                        {
                            auto missing = playback.update(document.song());
                            if (error.empty())
                                error = std::move(missing);
                            changed = true;
                        }
                        if (!error.empty())
                        {
                            view.setMessage(std::move(error), ui::MessageKind::Warning);
                            return true;
                        }
                    }
                    return changed;
                },
                .closing = closing,
                .woken = [&](std::int64_t now) { return view.inputWoken(now); }});
    return 0;
}
