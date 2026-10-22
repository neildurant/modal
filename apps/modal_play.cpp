// SPDX-License-Identifier: GPL-3.0-or-later

// modal-play: play a Standard MIDI File through a MIDI output port.

#include <modal/core/engine.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>
#include <modal/core/smf.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/console.hpp>
#include <modal/platform/midi_output.hpp>
#include <modal/platform/thread_priority.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace
{

using namespace modal;
using namespace std::chrono_literals;
using core::Tick;
using std::chrono::nanoseconds;

constexpr std::string_view kUsage = R"(Usage: modal-play --port NAME [options] FILE.mid
       modal-play --port NAME --panic

Plays a Standard MIDI File through a MIDI output port.

Options:
  --port NAME          the output port for every track: its full name, or any
                       part of it that matches only one port, ignoring case
  --start BAR[:BEAT]   where to start playing (default 1)
  --all-notes-off      send All Notes Off on each channel used when stopping
  --reset-sustain      send sustain off on each channel used when stopping
  --spin MS            how long before each event the engine stops sleeping and
                       busy-waits, for precise timing; 0 turns it off (default 1.5)
  --panic              send All Notes Off and Reset All Controllers on every
                       channel of the port, then exit
  --help               show this help

While playing:
  space                stop, or resume from where it stopped
  !                    panic: stop, then All Notes Off and Reset All
                       Controllers on every channel
  q, Esc or Ctrl-C     stop and exit
)";

struct Options
{
    std::string port;
    std::string file;
    core::BarBeatTick start{1, 1, 0};
    core::StopOptions stop;
    nanoseconds spin = platform::SystemClock::kDefaultSpinMargin;
    bool panic = false;
    bool help = false;
};

// The options, or why they're wrong.
std::variant<Options, std::string> parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::optional<std::string_view>
        {
            if (i + 1 < argc)
            {
                return argv[++i];
            }
            return std::nullopt;
        };

        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--port")
        {
            const auto port = value();
            if (!port)
            {
                return "--port needs a port name";
            }
            options.port = *port;
        }
        else if (arg == "--start")
        {
            const auto text = value();
            const auto start = text ? core::parseBarBeat(*text) : std::nullopt;
            if (!start)
            {
                return "--start needs a bar, or a bar and beat such as 17:3";
            }
            options.start = *start;
        }
        else if (arg == "--all-notes-off")
        {
            options.stop.allNotesOff = true;
        }
        else if (arg == "--reset-sustain")
        {
            options.stop.resetSustain = true;
        }
        else if (arg == "--spin")
        {
            const auto text = value();
            double ms = -1;
            if (text)
            {
                const auto* end = text->data() + text->size();
                const auto [ptr, error] = std::from_chars(text->data(), end, ms);
                if (error != std::errc{} || ptr != end)
                {
                    ms = -1;
                }
            }
            if (ms < 0 || ms > 10)
            {
                return "--spin needs a time from 0 to 10 ms";
            }
            options.spin = std::chrono::duration_cast<nanoseconds>(
                std::chrono::duration<double, std::milli>(ms));
        }
        else if (arg == "--panic")
        {
            options.panic = true;
        }
        else if (arg.starts_with("-"))
        {
            return "unknown option " + std::string{arg};
        }
        else if (options.file.empty())
        {
            options.file = arg;
        }
        else
        {
            return "only one file can be played";
        }
    }

    if (options.help)
    {
        return options;
    }
    if (options.port.empty())
    {
        return "--port is needed";
    }
    if (options.file.empty() && !options.panic)
    {
        return "no file given";
    }
    return options;
}

volatile std::sig_atomic_t gInterrupted = 0;

void onInterrupt(int)
{
    gInterrupted = 1;
}

std::string milliseconds(nanoseconds time)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(2)
         << std::chrono::duration<double, std::milli>(time).count() << " ms";
    return text.str();
}

std::string barBeat(const core::MeterMap& meter, Tick tick)
{
    const auto position = meter.toBarBeatTick(tick);
    return std::to_string(position.bar) + ":" + std::to_string(position.beat);
}

// Where the song's last event, or the end of its last note, is.
Tick songEnd(const core::Song& song)
{
    Tick end = 0;
    for (const auto& track : song.tracks())
    {
        for (const auto& event : song.events(track->id()).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            end = std::max(end, event.tick + (note != nullptr ? note->duration : 0));
        }
    }
    return end;
}

void printSong(const core::Song& song, const std::string& file)
{
    const auto& meter = song.meter();
    const auto signature = meter.signatureAt(0);
    const auto tempo = song.tempo();
    const Tick end = songEnd(song);
    const auto bars = end == 0 ? 0 : meter.toBarBeatTick(end - 1).bar;

    std::cout << (song.title.empty() ? std::filesystem::path(file).filename().string() : song.title)
              << '\n'
              << "  " << bars << " bars of " << signature.numerator << '/' << signature.denominator
              << " at " << std::fixed << std::setprecision(1) << tempo.tempoAt(0).bpm() << " BPM"
              << (tempo.changes().size() > 1 ? " (with tempo changes)" : "") << ", "
              << song.ppqn().value << " PPQN\n";

    std::size_t number = 1;
    for (const auto& track : song.tracks())
    {
        std::cout << "  " << std::setw(2) << number++ << "  ch " << std::setw(2) << track->channel
                  << "  " << std::setw(6) << song.events(track->id()).size() << " events  "
                  << track->name << '\n';
    }
}

void printTiming(std::string_view label, const core::TimingStats& stats)
{
    const auto p99 = stats.within(0.99);
    std::cout << "  " << std::left << std::setw(14) << label << std::right << "99% within "
              << std::setw(8) << (p99 ? milliseconds(*p99) : std::string{"> 20 ms"}) << "   max "
              << milliseconds(stats.max) << '\n';
}

void printSummary(const core::EngineStatus& status, std::size_t failedSends, nanoseconds spin)
{
    std::cout << "Sent " << status.lateness.count << " messages (spin margin " << milliseconds(spin)
              << ").\n";
    if (status.lateness.count > 0)
    {
        printTiming("Lateness", status.lateness);
        printTiming("  waking", status.wakeLateness);
        printTiming("  sending", status.sendTime);
    }
    if (status.droppedNotes > 0)
    {
        std::cout << status.droppedNotes
                  << " notes weren't played: more than 4,096 were sounding at once.\n";
    }
    if (failedSends > 0)
    {
        std::cout << failedSends << " messages couldn't be sent.\n";
    }
}

int panic(platform::MidiOutputs& outputs, core::DestinationId destination)
{
    // The scheduler's panic reaches the destinations its plans have used.
    core::PlaybackPlan plan{core::TempoMap{core::kPpqn}};
    plan.tracks.push_back({core::TrackId{1}, destination, 0, false, 0ns});
    core::Scheduler scheduler{outputs};
    scheduler.setPlan(plan, platform::now());
    scheduler.panic(platform::now());
    std::cout << "Sent All Notes Off and Reset All Controllers on all 16 channels.\n";
    return outputs.failedSends() == 0 ? 0 : 1;
}

int play(const Options& options, const core::SmfImportResult& imported,
         platform::MidiOutputs& outputs, core::DestinationId destination)
{
    const core::Song& song = *imported.song;
    for (const auto& warning : imported.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }
    printSong(song, options.file);

    const auto& meter = song.meter();
    const auto start = meter.toTick(options.start);
    if (!start)
    {
        std::cerr << "modal-play: there's no beat " << options.start.beat << " in bar "
                  << options.start.bar << '\n';
        return 2;
    }

    auto plan = std::make_shared<const core::PlaybackPlan>(
        core::compilePlan(song, [&](const core::Track&) { return destination; }));
    for (const auto& warning : plan->transposeWarnings)
    {
        std::cout << "Warning: track \"" << song.track(warning.track)->name << "\" loses "
                  << warning.droppedEvents << " notes transposed outside 0-127\n";
    }

    platform::SystemClock clock{options.spin};
    core::Engine engine{outputs, clock, options.stop};
    std::promise<std::string> priority;
    auto priorityError = priority.get_future();
    std::thread thread{[&]
                       {
                           const platform::RealtimePriority realtime;
                           priority.set_value(realtime.error());
                           engine.run();
                       }};
    if (const auto error = priorityError.get(); !error.empty())
    {
        std::cout << "Warning: the engine runs at normal priority, so timing may suffer: " << error
                  << '\n';
    }

    engine.setPlan(plan);
    engine.play(*start);
    bool playing = true;
    Tick stoppedAt = *start;

    std::signal(SIGINT, onInterrupt);
    std::cout << "\nspace: stop/resume   !: panic   q: quit\n";
    {
        platform::ConsoleKeys keys;
        bool quit = false;
        while (!quit && gInterrupted == 0)
        {
            const auto status = engine.status();
            while (const auto key = keys.poll())
            {
                switch (*key)
                {
                case ' ':
                    if (playing)
                    {
                        engine.stop();
                        stoppedAt = status.position.value_or(stoppedAt);
                    }
                    else
                    {
                        engine.play(stoppedAt);
                    }
                    playing = !playing;
                    break;
                case '!':
                    engine.panic();
                    stoppedAt = status.position.value_or(stoppedAt);
                    playing = false;
                    break;
                case 'q':
                case 'Q':
                case 0x1B: // Esc
                    quit = true;
                    break;
                default:
                    break;
                }
            }
            if (playing && status.finished)
            {
                break;
            }

            const Tick position = playing ? status.position.value_or(stoppedAt) : stoppedAt;
            std::cout << "\r  " << std::setw(9) << barBeat(meter, position)
                      << (playing ? "  playing" : "  stopped") << std::flush;
            std::this_thread::sleep_for(50ms);
        }
    }

    while (!engine.quit())
    {
        std::this_thread::sleep_for(1ms);
    }
    thread.join();
    std::cout << "\n\n";
    printSummary(engine.status(), outputs.failedSends(), options.spin);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const auto parsed = parseOptions(argc, argv);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        std::cerr << "modal-play: " << *error << "\n\n" << kUsage;
        return 2;
    }
    const auto& options = std::get<Options>(parsed);
    if (options.help)
    {
        std::cout << kUsage;
        return 0;
    }

    // Read the file first, so a bad one is reported without touching any MIDI port.
    std::optional<core::SmfImportResult> imported;
    if (!options.panic)
    {
        imported = core::importSmfFile(options.file);
        if (!imported->song)
        {
            std::cerr << "modal-play: " << options.file << ": " << imported->error << '\n';
            return 1;
        }
    }

    platform::MidiOutputs outputs;
    const auto port = outputs.open(options.port);
    if (!port.ok())
    {
        std::cerr << "modal-play: " << port.error << '\n';
        return 1;
    }
    std::cout << "Port: " << port.portName << "\n\n";

    return options.panic ? panic(outputs, port.destination)
                         : play(options, *imported, outputs, port.destination);
}
