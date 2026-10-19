// modal-record: record MIDI input to a Standard MIDI File, optionally playing along with one.

#include <modal/core/count_in.hpp>
#include <modal/core/engine.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/recorder.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/console.hpp>
#include <modal/platform/midi_input.hpp>
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
#include <vector>

namespace
{

using namespace modal;
using namespace std::chrono_literals;
using core::Tick;
using std::chrono::nanoseconds;

constexpr std::string_view kUsage = R"(Usage: modal-record --in NAME --out NAME [options] OUT.mid

Records MIDI input to a Standard MIDI File, onto a new track or, with --overdub,
into a track of the file played along with.

Ports (a full name, or any part of one that matches only one port, ignoring case):
  --in NAME            an input to record from; repeat for more
  --out NAME           the output for playback, MIDI thru and the metronome
  --click NAME         a different output for the metronome and count-in

What to record:
  --play FILE.mid      play along with a file; the take is added to it
  --overdub N          merge into track N of the play-along file, instead of
                       recording onto a new track
  --channel N          the new track's channel, 1-16 (default 1)
  --start BAR[:BEAT]   where recording starts (default 1)
  --tempo BPM          the tempo, without --play (default 120)
  --meter N/D          the time signature, without --play (default 4/4)

Before recording (one of these; default a 1-bar count-in):
  --count-in BARS      a count-in of this many bars
  --count-in-beats N   a count-in of this many beats
  --pre-roll BARS      play this many bars of the song before the start
  --no-count-in        start recording at once

Other options:
  --no-click           no metronome while recording (a count-in still clicks)
  --no-thru            don't send the input on to the output
  --type0              write a type 0 file (default type 1)
  --force              overwrite OUT.mid if it exists
  --spin MS            how long before each event the engine stops sleeping and
                       busy-waits, for precise timing; 0 turns it off (default 1.5)
  --help               show this help

While recording:
  space                stop, save and exit
  Esc or Ctrl-C        stop and exit without saving (asks first)
  !                    panic: stop, then All Notes Off and Reset All
                       Controllers on every channel; exits without saving
)";

struct Options
{
    std::vector<std::string> inputs;
    std::string output;
    std::string click;
    std::string play;
    std::optional<int> overdub; // track number, from 1
    int channel = 1;
    core::BarBeatTick start{1, 1, 0};
    std::optional<double> tempo;
    std::optional<core::TimeSignature> meter;
    core::CountInSettings countIn;
    bool noClick = false;
    bool noThru = false;
    core::SmfType type = core::SmfType::Type1;
    bool force = false;
    nanoseconds spin = platform::SystemClock::kDefaultSpinMargin;
    std::string file;
    bool help = false;
};

template <typename T>
std::optional<T> parseNumber(std::string_view text)
{
    T value{};
    const auto* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parseInt(std::optional<std::string_view> text, int min, int max)
{
    const auto value = text ? parseNumber<int>(*text) : std::nullopt;
    if (!value || *value < min || *value > max)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<core::TimeSignature> parseMeter(std::string_view text)
{
    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto numerator = parseNumber<std::int32_t>(text.substr(0, slash));
    const auto denominator = parseNumber<std::int32_t>(text.substr(slash + 1));
    if (!numerator || !denominator)
    {
        return std::nullopt;
    }
    const core::TimeSignature signature{*numerator, *denominator};
    if (!signature.isValid())
    {
        return std::nullopt;
    }
    return signature;
}

// The options, or why they're wrong.
std::variant<Options, std::string> parseOptions(int argc, char** argv)
{
    using Mode = core::CountInSettings::Mode;
    Options options;
    int leadOptions = 0; // count-in or pre-roll options given

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
        const auto name = [&](std::string& into,
                              std::string_view option) -> std::optional<std::string>
        {
            const auto text = value();
            if (!text)
            {
                return std::string{option} + " needs a port name";
            }
            into = *text;
            return std::nullopt;
        };

        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--in")
        {
            const auto text = value();
            if (!text)
            {
                return "--in needs a port name";
            }
            if (options.inputs.size() == core::Engine::kMaxInputs)
            {
                return "at most " + std::to_string(core::Engine::kMaxInputs) + " inputs";
            }
            options.inputs.emplace_back(*text);
        }
        else if (arg == "--out")
        {
            if (auto error = name(options.output, arg))
            {
                return *error;
            }
        }
        else if (arg == "--click")
        {
            if (auto error = name(options.click, arg))
            {
                return *error;
            }
        }
        else if (arg == "--play")
        {
            const auto text = value();
            if (!text)
            {
                return "--play needs a MIDI file";
            }
            options.play = *text;
        }
        else if (arg == "--overdub")
        {
            options.overdub = parseInt(value(), 1, 9999);
            if (!options.overdub)
            {
                return "--overdub needs a track number";
            }
        }
        else if (arg == "--channel")
        {
            const auto channel = parseInt(value(), 1, 16);
            if (!channel)
            {
                return "--channel needs a channel from 1 to 16";
            }
            options.channel = *channel;
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
        else if (arg == "--tempo")
        {
            const auto text = value();
            options.tempo = text ? parseNumber<double>(*text) : std::nullopt;
            if (!options.tempo || *options.tempo < 1 || *options.tempo > 1000)
            {
                return "--tempo needs a tempo from 1 to 1000 BPM";
            }
        }
        else if (arg == "--meter")
        {
            const auto text = value();
            options.meter = text ? parseMeter(*text) : std::nullopt;
            if (!options.meter)
            {
                return "--meter needs a time signature such as 3/4 or 7/8";
            }
        }
        else if (arg == "--count-in" || arg == "--count-in-beats" || arg == "--pre-roll")
        {
            const auto count = parseInt(value(), 1, 64);
            if (!count)
            {
                return std::string{arg} + " needs a number from 1 to 64";
            }
            ++leadOptions;
            auto& countIn = options.countIn;
            countIn.mode = arg == "--pre-roll" ? Mode::PreRoll : Mode::CountIn;
            countIn.bars = arg == "--count-in" ? *count : countIn.bars;
            countIn.beats = arg == "--count-in-beats" ? *count : 0;
            countIn.preRollBars = arg == "--pre-roll" ? *count : countIn.preRollBars;
        }
        else if (arg == "--no-count-in")
        {
            ++leadOptions;
            options.countIn.mode = Mode::None;
        }
        else if (arg == "--no-click")
        {
            options.noClick = true;
        }
        else if (arg == "--no-thru")
        {
            options.noThru = true;
        }
        else if (arg == "--type0")
        {
            options.type = core::SmfType::Type0;
        }
        else if (arg == "--force")
        {
            options.force = true;
        }
        else if (arg == "--spin")
        {
            const auto text = value();
            const auto ms = text ? parseNumber<double>(*text) : std::nullopt;
            if (!ms || *ms < 0 || *ms > 10)
            {
                return "--spin needs a time from 0 to 10 ms";
            }
            options.spin = std::chrono::duration_cast<nanoseconds>(
                std::chrono::duration<double, std::milli>(*ms));
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
            return "only one file can be written";
        }
    }

    if (options.help)
    {
        return options;
    }
    if (options.inputs.empty())
    {
        return "--in is needed";
    }
    if (options.output.empty())
    {
        return "--out is needed";
    }
    if (options.file.empty())
    {
        return "no file to write given";
    }
    if (leadOptions > 1)
    {
        return "choose one of --count-in, --count-in-beats, --pre-roll and --no-count-in";
    }
    if (!options.play.empty() && (options.tempo || options.meter))
    {
        return "--tempo and --meter can't be used with --play: the file has its own";
    }
    if (options.play.empty() && options.overdub)
    {
        return "--overdub needs a file to --play";
    }
    std::error_code error;
    if (!options.force && std::filesystem::exists(options.file, error))
    {
        return options.file + " exists; use --force to overwrite it";
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
    const auto position = meter.toBarBeatTick(std::max(tick, Tick{0}));
    return std::to_string(position.bar) + ":" + std::to_string(position.beat);
}

void printTiming(std::string_view label, const core::TimingStats& stats)
{
    const auto p99 = stats.within(0.99);
    std::cout << "  " << std::left << std::setw(14) << label << std::right << "99% within "
              << std::setw(8) << (p99 ? milliseconds(*p99) : std::string{"> 20 ms"}) << "   max "
              << milliseconds(stats.max) << '\n';
}

// The song to record into, with the track to record onto armed, or why there isn't one.
struct Prepared
{
    core::Song song;
    core::TrackId track;
};

std::variant<Prepared, std::string> prepareSong(const Options& options)
{
    core::Song song{core::kPpqn};
    if (!options.play.empty())
    {
        auto imported = core::importSmfFile(options.play);
        if (!imported.song)
        {
            return options.play + ": " + imported.error;
        }
        for (const auto& warning : imported.warnings)
        {
            std::cout << "Warning: " << warning << '\n';
        }
        song = std::move(*imported.song);
    }
    else
    {
        song.updateTempo([&](core::TempoMap& tempo)
                         { tempo.set(0, core::Tempo::fromBpm(options.tempo.value_or(120.0))); });
        if (options.meter)
        {
            song.updateMeter([&](core::MeterMap& meter) { meter.set(0, *options.meter); });
        }
    }

    core::TrackId track{0};
    if (options.overdub)
    {
        const auto index = static_cast<std::size_t>(*options.overdub - 1);
        if (index >= song.tracks().size())
        {
            return options.play + " has no track " + std::to_string(*options.overdub);
        }
        track = song.tracks()[index]->id();
    }
    else
    {
        track = song.addTrack("Recorded");
        song.updateTrack(track, [&](core::Track& t) { t.channel = options.channel; });
    }
    song.updateTrack(track,
                     [&](core::Track& t)
                     {
                         t.arm = true;
                         t.thru = !options.noThru;
                     });

    song.metronome.when = options.noClick ? core::MetronomeSettings::When::Off
                                          : core::MetronomeSettings::When::Recording;
    song.countIn = options.countIn;
    return Prepared{std::move(song), track};
}

// Notes and other events on a track starting in [from, to).
std::pair<std::size_t, std::size_t> countEvents(const core::EventList& events, Tick from, Tick to)
{
    std::size_t notes = 0;
    std::size_t others = 0;
    for (const auto& event : events.startingIn(from, to))
    {
        ++(event.isNote() ? notes : others);
    }
    return {notes, others};
}

// Asks whether to discard the take, while recording carries on.
bool confirmDiscard(platform::ConsoleKeys& keys)
{
    std::cout << "\nStop without saving? (y/n) " << std::flush;
    gInterrupted = 0;
    for (;;)
    {
        while (const auto key = keys.poll())
        {
            if (*key == 'y' || *key == 'Y')
            {
                return true;
            }
            if (*key == 'n' || *key == 'N' || *key == 0x1B)
            {
                std::cout << "no\n";
                return false;
            }
        }
        if (gInterrupted != 0) // a second Ctrl-C
        {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
}

enum class Ending : std::uint8_t
{
    Save,
    Discard,
    Panic,
};

int record(const Options& options, Prepared prepared)
{
    auto& song = prepared.song;
    const auto& meter = song.meter();
    const auto recordPoint = meter.toTick(options.start);
    if (!recordPoint)
    {
        std::cerr << "modal-record: there's no beat " << options.start.beat << " in bar "
                  << options.start.bar << '\n';
        return 2;
    }

    platform::MidiOutputs outputs;
    const auto out = outputs.open(options.output);
    if (!out.ok())
    {
        std::cerr << "modal-record: " << out.error << '\n';
        return 1;
    }
    std::cout << "Out:   " << out.portName << '\n';
    auto clickDestination = out.destination;
    if (!options.click.empty())
    {
        const auto click = outputs.open(options.click);
        if (!click.ok())
        {
            std::cerr << "modal-record: " << click.error << '\n';
            return 1;
        }
        std::cout << "Click: " << click.portName << '\n';
        clickDestination = click.destination;
    }

    platform::SystemClock clock{options.spin};
    core::Engine engine{outputs, clock};
    engine.setThru(!options.noThru);

    std::vector<std::unique_ptr<platform::MidiInput>> inputs;
    for (std::size_t i = 0; i < options.inputs.size(); ++i)
    {
        const core::InputId id{static_cast<std::uint32_t>(i)};
        auto& input = inputs.emplace_back(std::make_unique<platform::MidiInput>(
            [&engine, id](std::span<const std::uint8_t> message, nanoseconds time)
            { engine.receive(id, message, time); }));
        const auto opened = input->open(options.inputs[i]);
        if (!opened.ok())
        {
            std::cerr << "modal-record: " << opened.error << '\n';
            return 1;
        }
        std::cout << "In:    " << opened.portName << '\n';
    }

    const auto plan = std::make_shared<const core::PlaybackPlan>(
        core::compilePlan(song, [&](const core::Track&) { return out.destination; },
                          {.recording = true, .metronomeDestination = clickDestination}));
    auto start = core::planRecordStart(song, *recordPoint, clickDestination);
    const nanoseconds countInLength = start.countIn ? start.countIn->length : 0ns;

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

    const auto& track = *song.track(prepared.track);
    std::cout << "\nRecording onto track \"" << track.name << "\" (channel " << track.channel
              << ") from " << barBeat(meter, *recordPoint) << ".\n"
              << "space: stop and save   Esc: stop without saving   !: panic\n";

    engine.setPlan(plan);
    engine.play(start.playFrom, std::move(start.countIn));
    const auto play = engine.lastPlay();
    const auto countInEnds = platform::now() + countInLength;
    core::Recorder recorder{song, play, *recordPoint,
                            options.overdub ? core::RecordMode::Overdub
                                            : core::RecordMode::Replace};

    std::signal(SIGINT, onInterrupt);
    Ending ending = Ending::Save;
    {
        platform::ConsoleKeys keys;
        bool done = false;
        while (!done)
        {
            while (const auto stamped = engine.nextReceived())
            {
                recorder.receive(*stamped);
            }
            while (const auto key = keys.poll())
            {
                if (*key == ' ')
                {
                    done = true;
                }
                else if (*key == '!')
                {
                    ending = Ending::Panic;
                    done = true;
                }
                else if (*key == 0x1B && confirmDiscard(keys))
                {
                    ending = Ending::Discard;
                    done = true;
                }
            }
            if (gInterrupted != 0 && confirmDiscard(keys))
            {
                ending = Ending::Discard;
                done = true;
            }
            if (done)
            {
                break;
            }

            const auto status = engine.status();
            const bool countingIn = platform::now() < countInEnds;
            std::cout << "\r  " << std::setw(9) << barBeat(meter, status.position.value_or(0))
                      << (countingIn ? "  counting in" : "  recording  ") << "  "
                      << status.inputReceived << " messages in" << std::flush;
            std::this_thread::sleep_for(20ms);
        }
    }

    if (ending == Ending::Panic)
    {
        engine.panic();
    }
    else
    {
        engine.stop();
    }
    std::optional<Tick> stoppedAt;
    while (!(stoppedAt = engine.stoppedAt(play)))
    {
        std::this_thread::sleep_for(1ms);
    }
    while (const auto stamped = engine.nextReceived())
    {
        recorder.receive(*stamped);
    }
    while (!engine.quit())
    {
        std::this_thread::sleep_for(1ms);
    }
    thread.join();
    inputs.clear(); // before the engine they send to

    const auto status = engine.status();
    std::cout << "\n\n";
    int result = 0;
    if (ending != Ending::Save)
    {
        std::cout << "Stopped without saving.\n";
    }
    else if (*stoppedAt <= *recordPoint)
    {
        std::cout << "Stopped before the start: nothing recorded, nothing saved.\n";
    }
    else
    {
        const auto recorded = recorder.finish(*stoppedAt);
        const auto [notesBefore, othersBefore] =
            countEvents(song.events(prepared.track), *recordPoint, *stoppedAt);
        const auto [notes, others] =
            countEvents(recorded.events(prepared.track), *recordPoint, *stoppedAt);
        std::cout << "Recorded " << barBeat(meter, *recordPoint) << " to "
                  << barBeat(meter, *stoppedAt) << ": " << notes << " notes and " << others
                  << " other events";
        if (options.overdub)
        {
            std::cout << " (" << notesBefore << " notes and " << othersBefore
                      << " other events there before)";
        }
        std::cout << ".\n";

        const auto written = core::exportSmfFile(recorded, options.file, options.type);
        for (const auto& warning : written.warnings)
        {
            std::cout << "Warning: " << warning << '\n';
        }
        if (written.error.empty())
        {
            std::cout << "Saved " << options.file << ".\n";
        }
        else
        {
            std::cerr << "modal-record: " << written.error << '\n';
            result = 1;
        }
    }

    std::cout << "\nInput: " << status.inputReceived << " messages";
    if (status.inputDropped + status.inputUnreported > 0)
    {
        std::cout << ", " << status.inputDropped << " dropped and " << status.inputUnreported
                  << " not passed back for recording";
    }
    std::cout << ".\n";
    if (status.thruDelay.count > 0)
    {
        printTiming("Thru delay", status.thruDelay);
    }
    if (status.lateness.count > 0)
    {
        printTiming("Lateness", status.lateness);
        printTiming("  waking", status.wakeLateness);
        printTiming("  sending", status.sendTime);
    }
    if (status.droppedNotes + status.thruDroppedNotes > 0)
    {
        std::cout << status.droppedNotes << " played and " << status.thruDroppedNotes
                  << " thru notes weren't sent: too many were sounding at once.\n";
    }
    if (outputs.failedSends() > 0)
    {
        std::cout << outputs.failedSends() << " messages couldn't be sent.\n";
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    const auto parsed = parseOptions(argc, argv);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        std::cerr << "modal-record: " << *error << "\n\n" << kUsage;
        return 2;
    }
    const auto& options = std::get<Options>(parsed);
    if (options.help)
    {
        std::cout << kUsage;
        return 0;
    }

    // Read the play-along file first, so a bad one is reported without touching any port.
    auto prepared = prepareSong(options);
    if (const auto* error = std::get_if<std::string>(&prepared))
    {
        std::cerr << "modal-record: " << *error << '\n';
        return 1;
    }
    return record(options, std::move(std::get<Prepared>(prepared)));
}
