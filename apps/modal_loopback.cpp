// modal-loopback: measure MIDI timing through a cable from an output port back to an input.
//
// Plays a steady stream of notes through the engine, as modal-play does, and times each
// message from when it was due to when it came back. Each note's pitch and velocity identify
// it, so every message received is matched to the one sent.
//
// With --record, the stream is also recorded, as modal-record would record it, and each
// recorded note is compared with the one sent.

#include <modal/core/engine.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/recorder.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song.hpp>
#include <modal/core/timing_stats.hpp>
#include <modal/platform/clock.hpp>
#include <modal/platform/midi_input.hpp>
#include <modal/platform/midi_output.hpp>
#include <modal/platform/thread_priority.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

namespace
{

using namespace modal;
using namespace std::chrono_literals;
using std::chrono::nanoseconds;

constexpr std::string_view kUsage = R"(Usage: modal-loopback --port NAME [options]
       modal-loopback --out NAME --in NAME [options]

Measures MIDI timing through a cable from an output port back to an input port.
Sends a steady stream of notes and reports how long each message took to come
back, from the time it was due.

Options:
  --port NAME      the output and input port, when they have the same name
  --out NAME       the output port
  --in NAME        the input port
  --count N        notes to send (default 2000)
  --interval MS    time between note-ons, in milliseconds (default 10); each
                   note lasts half of it
  --chord N        notes sent at once, 1 to 16 (default 1)
  --spin MS        how long before each event the engine stops sleeping and
                   busy-waits, for precise timing; 0 turns it off (default 1.5)
  --record         also record the stream, at 120 BPM, and compare each
                   recorded note with the one sent
  --save FILE.mid  with --record, save what was sent and recorded, as two tracks
  --force          overwrite the --save file if it exists
  --help           show this help

Port names can be the full name, or any part that matches only one port,
ignoring case. A 3-byte message takes about 1 ms to cross a MIDI cable, so with
chords, later notes arrive later.
)";

struct Options
{
    std::string out;
    std::string in;
    int count = 2000;
    double intervalMs = 10.0;
    int chord = 1;
    nanoseconds spin = platform::SystemClock::kDefaultSpinMargin;
    bool record = false;
    std::string save;
    bool force = false;
    bool help = false;
};

template <typename T>
std::optional<T> number(std::string_view text)
{
    T value{};
    const auto* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

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
        else if (arg == "--port" || arg == "--out" || arg == "--in")
        {
            const auto name = value();
            if (!name)
            {
                return std::string{arg} + " needs a port name";
            }
            if (arg != "--in")
            {
                options.out = *name;
            }
            if (arg != "--out")
            {
                options.in = *name;
            }
        }
        else if (arg == "--count")
        {
            const auto text = value();
            const auto count = text ? number<int>(*text) : std::nullopt;
            if (!count || *count < 1 || *count > 100'000)
            {
                return "--count needs a number of notes from 1 to 100000";
            }
            options.count = *count;
        }
        else if (arg == "--interval")
        {
            const auto text = value();
            const auto interval = text ? number<double>(*text) : std::nullopt;
            if (!interval || *interval < 1.0 || *interval > 1000.0)
            {
                return "--interval needs a time from 1 to 1000 ms";
            }
            options.intervalMs = *interval;
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
        else if (arg == "--chord")
        {
            const auto text = value();
            const auto chord = text ? number<int>(*text) : std::nullopt;
            if (!chord || *chord < 1 || *chord > 16)
            {
                return "--chord needs a number of notes from 1 to 16";
            }
            options.chord = *chord;
        }
        else if (arg == "--record")
        {
            options.record = true;
        }
        else if (arg == "--save")
        {
            const auto file = value();
            if (!file)
            {
                return "--save needs a MIDI file";
            }
            options.save = *file;
        }
        else if (arg == "--force")
        {
            options.force = true;
        }
        else
        {
            return "unknown option " + std::string{arg};
        }
    }
    if (options.help)
    {
        return options;
    }
    if (options.out.empty() || options.in.empty())
    {
        return "both an output and an input port are needed (--port, or --out and --in)";
    }
    if (!options.save.empty() && !options.record)
    {
        return "--save needs --record";
    }
    std::error_code error;
    if (!options.save.empty() && !options.force && std::filesystem::exists(options.save, error))
    {
        return options.save + " exists; use --force to overwrite it";
    }
    return options;
}

// A message as sent or received: a note-on or note-off.
struct Message
{
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;
    nanoseconds time{0};
};

// Passes messages on to the MIDI outputs, noting when each was due and when its send call
// returned. Runs on the engine thread, into space reserved up front.
class RecordingSink final : public core::MessageSink
{
public:
    RecordingSink(core::MessageSink& inner, std::size_t capacity)
        : inner_(inner)
    {
        sent.reserve(capacity);
        due.reserve(capacity);
    }

    void send(core::DestinationId destination, std::span<const std::uint8_t> bytes,
              nanoseconds dueTime) noexcept override
    {
        inner_.send(destination, bytes, dueTime);
        const auto done = platform::now();
        if (sent.size() < sent.capacity() && bytes.size() <= 3)
        {
            Message message;
            std::copy(bytes.begin(), bytes.end(), message.bytes.begin());
            message.size = static_cast<std::uint8_t>(bytes.size());
            message.time = done;
            sent.push_back(message);
            due.push_back(dueTime);
        }
    }

    std::vector<Message> sent; // with the time each send call returned
    std::vector<nanoseconds> due;

private:
    core::MessageSink& inner_;
};

struct Summary
{
    nanoseconds min{0};
    nanoseconds median{0};
    nanoseconds p99{0};
    nanoseconds max{0};
    nanoseconds deviation{0}; // standard deviation
};

// The value a fraction of the way through sorted values, by the nearest-rank method.
template <typename T>
T percentile(const std::vector<T>& sorted, double fraction)
{
    const auto rank =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

Summary summarize(std::vector<nanoseconds> values)
{
    Summary summary;
    if (values.empty())
    {
        return summary;
    }
    std::ranges::sort(values);
    summary.min = values.front();
    summary.median = percentile(values, 0.5);
    summary.p99 = percentile(values, 0.99);
    summary.max = values.back();

    double mean = 0;
    for (const auto value : values)
    {
        mean += static_cast<double>(value.count());
    }
    mean /= static_cast<double>(values.size());
    double variance = 0;
    for (const auto value : values)
    {
        const double difference = static_cast<double>(value.count()) - mean;
        variance += difference * difference;
    }
    variance /= static_cast<double>(values.size());
    summary.deviation = nanoseconds{static_cast<std::int64_t>(std::sqrt(variance))};
    return summary;
}

std::string milliseconds(nanoseconds time)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(2)
         << std::chrono::duration<double, std::milli>(time).count() << " ms";
    return text.str();
}

void printSummary(std::string_view label, const Summary& summary)
{
    std::cout << "  " << std::left << std::setw(22) << label << std::right << "min " << std::setw(8)
              << milliseconds(summary.min) << "  median " << std::setw(8)
              << milliseconds(summary.median) << "  p99 " << std::setw(8)
              << milliseconds(summary.p99) << "  max " << std::setw(8) << milliseconds(summary.max)
              << '\n';
}

void printTicks(std::string_view label, std::vector<core::Tick> values)
{
    if (values.empty())
    {
        return;
    }
    std::ranges::sort(values);
    std::cout << "  " << std::left << std::setw(22) << label << std::right << "min " << std::setw(5)
              << values.front() << "     median " << std::setw(5) << percentile(values, 0.5)
              << "     p99 " << std::setw(5) << percentile(values, 0.99) << "     max "
              << std::setw(5) << values.back() << '\n';
}

void printTiming(std::string_view label, const core::TimingStats& stats)
{
    const auto p99 = stats.within(0.99);
    std::cout << "  " << std::left << std::setw(22) << label << std::right << "99% within "
              << std::setw(8) << (p99 ? milliseconds(*p99) : std::string{"> 20 ms"}) << "  max "
              << milliseconds(stats.max) << '\n';
}

bool isNoteOn(const Message& message)
{
    return message.size == 3 && (message.bytes[0] & 0xF0) == 0x90 && message.bytes[2] != 0;
}

bool isNoteOff(const Message& message)
{
    return message.size == 3 && ((message.bytes[0] & 0xF0) == 0x80 ||
                                 ((message.bytes[0] & 0xF0) == 0x90 && message.bytes[2] == 0));
}

constexpr nanoseconds kLeadIn = 200ms; // time for the engine and port to settle

nanoseconds intervalOf(const Options& options)
{
    return std::chrono::duration_cast<nanoseconds>(
        std::chrono::duration<double, std::milli>(options.intervalMs));
}

// The stream: note n (counting across chords) has pitch n % 128 and velocity
// 1 + (n / 128) % 127, which tells apart the first 16,256 notes; its note-off carries the
// same velocity as its release velocity.
std::uint8_t pitchOf(int n)
{
    return static_cast<std::uint8_t>(n % 128);
}

std::uint8_t velocityOf(int n)
{
    return static_cast<std::uint8_t>(1 + (n / 128) % 127);
}

core::PlaybackPlan streamPlan(const Options& options, core::DestinationId destination)
{
    const auto interval = intervalOf(options);
    core::PlaybackPlan plan{core::TempoMap{core::kPpqn}};
    plan.tracks.push_back({core::TrackId{1}, destination, 0, false, 0ns});
    for (int i = 0; i < options.count; ++i)
    {
        for (int c = 0; c < options.chord; ++c)
        {
            const int n = i * options.chord + c;
            const auto pitch = pitchOf(n);
            const auto velocity = velocityOf(n);
            core::PlannedEvent event{};
            event.time = kLeadIn + interval * i;
            event.tick = i;
            event.track = core::TrackId{1};
            event.destination = destination;
            event.message = {{0x90, pitch, velocity}, 3};
            event.endTick = i;
            event.noteOffTime = event.time + interval / 2;
            event.releaseVelocity = velocity;
            plan.events.push_back(event);
        }
    }
    return plan;
}

// For --record: the stream as a song at 120 BPM, its notes on a track "Sent" at the ticks at
// or before their times in the plain stream, and an empty track armed to record onto.
struct StreamSong
{
    core::Song song;
    core::TrackId sent{0};
    core::TrackId recorded{0};
};

StreamSong streamSong(const Options& options)
{
    StreamSong stream;
    auto& song = stream.song;
    song.tempo().set(0, core::Tempo::fromBpm(120.0));
    song.metronome.when = core::MetronomeSettings::When::Off;
    song.countIn.mode = core::CountInSettings::Mode::None;

    const auto interval = intervalOf(options);
    const auto& tempo = song.tempo();
    stream.sent = song.addTrack("Sent");
    song.updateTrack(stream.sent,
                     [&](core::Track& track)
                     {
                         for (int i = 0; i < options.count; ++i)
                         {
                             const auto on = tempo.toTick(kLeadIn + interval * i);
                             const auto off = tempo.toTick(kLeadIn + interval * i + interval / 2);
                             for (int c = 0; c < options.chord; ++c)
                             {
                                 const int n = i * options.chord + c;
                                 track.events.insert({on, core::Note{pitchOf(n), velocityOf(n),
                                                                     off - on, velocityOf(n)}});
                             }
                         }
                     });
    stream.recorded = song.addTrack("Recorded");
    song.updateTrack(stream.recorded,
                     [](core::Track& track)
                     {
                         track.arm = true;
                         track.thru = false;
                     });
    return stream;
}

struct Matching
{
    std::vector<nanoseconds> latency;   // from due to arrival
    std::vector<nanoseconds> afterSend; // from the send call returning to arrival
    std::size_t lost = 0;
    std::size_t outOfOrder = 0;
    std::size_t unexpected = 0;
};

// Matches each message received to the one sent: note-ons by pitch and velocity, note-offs
// by pitch and velocity too, or by pitch alone when a device turned them into note-ons
// with velocity 0.
Matching match(const RecordingSink& sink, const std::vector<Message>& received)
{
    using Key = std::tuple<bool, std::uint8_t, std::uint8_t>; // note-on, pitch, velocity
    std::map<Key, std::deque<std::size_t>> byKey;
    std::map<std::uint8_t, std::deque<std::size_t>> offsByPitch;
    for (std::size_t i = 0; i < sink.sent.size(); ++i)
    {
        const auto& message = sink.sent[i];
        const bool on = isNoteOn(message);
        byKey[{on, message.bytes[1], message.bytes[2]}].push_back(i);
        if (!on)
        {
            offsByPitch[message.bytes[1]].push_back(i);
        }
    }

    std::vector<bool> matched(sink.sent.size(), false);
    const auto take = [&](std::deque<std::size_t>& queue) -> std::optional<std::size_t>
    {
        while (!queue.empty() && matched[queue.front()])
        {
            queue.pop_front();
        }
        if (queue.empty())
        {
            return std::nullopt;
        }
        const auto index = queue.front();
        queue.pop_front();
        matched[index] = true;
        return index;
    };

    Matching result;
    std::optional<std::size_t> latest;
    for (const auto& message : received)
    {
        std::optional<std::size_t> index;
        if (isNoteOn(message) || (message.size == 3 && (message.bytes[0] & 0xF0) == 0x80))
        {
            if (const auto it = byKey.find({isNoteOn(message), message.bytes[1], message.bytes[2]});
                it != byKey.end())
            {
                index = take(it->second);
            }
        }
        if (!index && isNoteOff(message))
        {
            if (const auto it = offsByPitch.find(message.bytes[1]); it != offsByPitch.end())
            {
                index = take(it->second);
            }
        }
        if (!index)
        {
            ++result.unexpected;
            continue;
        }
        result.latency.push_back(message.time - sink.due[*index]);
        result.afterSend.push_back(message.time - sink.sent[*index].time);
        if (latest && *index < *latest)
        {
            ++result.outOfOrder;
        }
        latest = std::max(latest.value_or(0), *index);
    }
    result.lost = static_cast<std::size_t>(std::ranges::count(matched, false));
    return result;
}

// How the recorded notes compare with the ones sent, matched by pitch and velocity.
struct RecordCheck
{
    std::vector<nanoseconds> late;     // recorded position less the one sent
    std::vector<core::Tick> lateTicks; // the same, in ticks
    std::vector<nanoseconds> longer;   // recorded length less the one sent
    std::size_t missing = 0;           // sent and not recorded
    std::size_t extra = 0;             // recorded and not sent
    std::size_t offArrival = 0;        // not at the tick of its note-on's arrival time
};

// origin is when the song started, on the engine's clock.
RecordCheck checkRecording(const StreamSong& stream, const core::Song& recorded,
                           const std::vector<Message>& arrivals, nanoseconds origin)
{
    using Key = std::pair<std::uint8_t, std::uint8_t>; // pitch, velocity
    std::map<Key, std::deque<const core::Event*>> sent;
    for (const auto& event : stream.song.track(stream.sent)->events.all())
    {
        const auto& note = std::get<core::Note>(event.data);
        sent[{note.pitch, note.velocity}].push_back(&event);
    }
    std::map<Key, std::deque<nanoseconds>> arrived; // note-on arrival times
    for (const auto& message : arrivals)
    {
        if (isNoteOn(message))
        {
            arrived[{message.bytes[1], message.bytes[2]}].push_back(message.time);
        }
    }

    const auto& tempo = recorded.tempo();
    const auto timeOf = [&](core::Tick tick)
    { return tempo.toTime(std::max(tick, core::Tick{0})); };
    RecordCheck check;
    for (const auto& event : recorded.track(stream.recorded)->events.all())
    {
        const auto* note = std::get_if<core::Note>(&event.data);
        if (note == nullptr)
        {
            ++check.extra;
            continue;
        }
        const Key key{note->pitch, note->velocity};
        const auto it = sent.find(key);
        if (it == sent.end() || it->second.empty())
        {
            ++check.extra;
            continue;
        }
        const auto& original = *it->second.front();
        it->second.pop_front();
        const auto originalLength = std::get<core::Note>(original.data).duration;

        check.lateTicks.push_back(event.tick - original.tick);
        check.late.push_back(timeOf(event.tick) - timeOf(original.tick));
        check.longer.push_back((timeOf(event.tick + note->duration) - timeOf(event.tick)) -
                               (timeOf(original.tick + originalLength) - timeOf(original.tick)));

        auto& times = arrived[key];
        if (times.empty() || tempo.toTickExtended(times.front() - origin) != event.tick)
        {
            ++check.offArrival;
        }
        if (!times.empty())
        {
            times.pop_front();
        }
    }
    for (const auto& [key, left] : sent)
    {
        check.missing += left.size();
    }
    return check;
}

int run(const Options& options)
{
    platform::MidiOutputs outputs;
    const auto out = outputs.open(options.out);
    if (!out.ok())
    {
        std::cerr << "modal-loopback: " << out.error << '\n';
        return 1;
    }

    const std::size_t expected =
        static_cast<std::size_t>(options.count) * static_cast<std::size_t>(options.chord) * 2;
    RecordingSink sink{outputs, expected};
    platform::SystemClock clock{options.spin};
    core::Engine engine{sink, clock};
    engine.setThru(false); // thru would send everything round the cable again

    std::mutex mutex;
    std::vector<Message> received;
    received.reserve(expected);
    platform::MidiInput input{[&](std::span<const std::uint8_t> bytes, nanoseconds time)
                              {
                                  if (options.record)
                                  {
                                      engine.receive(core::InputId{0}, bytes, time);
                                  }
                                  Message message;
                                  const auto size = std::min<std::size_t>(bytes.size(), 3);
                                  std::copy_n(bytes.begin(), size, message.bytes.begin());
                                  message.size = static_cast<std::uint8_t>(size);
                                  message.time = time;
                                  const std::lock_guard lock{mutex};
                                  received.push_back(message);
                              }};
    const auto in = input.open(options.in);
    if (!in.ok())
    {
        std::cerr << "modal-loopback: " << in.error << '\n';
        return 1;
    }

    std::cout << "Out: " << out.portName << "\nIn:  " << in.portName << "\n\n"
              << (options.record ? "Recording " : "Sending ") << options.count
              << (options.chord > 1 ? " chords of " : " notes")
              << (options.chord > 1 ? std::to_string(options.chord) + " notes" : "")
              << ", one every " << options.intervalMs << " ms, spin margin "
              << milliseconds(options.spin) << "...\n";

    std::optional<StreamSong> stream;
    std::shared_ptr<const core::PlaybackPlan> plan;
    if (options.record)
    {
        stream = streamSong(options);
        plan = std::make_shared<const core::PlaybackPlan>(
            core::compilePlan(stream->song, [&](const core::Track&) { return out.destination; },
                              {.recording = true, .metronomeDestination = std::nullopt}));
    }
    else
    {
        plan = std::make_shared<const core::PlaybackPlan>(streamPlan(options, out.destination));
    }

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
        std::cout << "Warning: the engine runs at normal priority: " << error << '\n';
    }

    {
        const std::lock_guard lock{mutex};
        received.clear(); // anything that arrived before the stream
    }
    engine.setPlan(plan);
    engine.play(0);
    const auto play = engine.lastPlay();
    std::optional<core::Recorder> recorder;
    if (stream)
    {
        recorder.emplace(stream->song, play, 0, core::RecordMode::Replace);
    }
    const auto takeInput = [&]
    {
        while (const auto stamped = engine.nextReceived())
        {
            if (recorder)
            {
                recorder->receive(*stamped);
            }
        }
    };

    while (!engine.status().finished)
    {
        takeInput();
        std::this_thread::sleep_for(10ms);
    }

    // Wait for the last messages to come back.
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        takeInput();
        {
            const std::lock_guard lock{mutex};
            if (received.size() >= expected)
            {
                break;
            }
        }
        std::this_thread::sleep_for(10ms);
    }
    engine.stop();
    std::optional<core::Tick> stoppedAt;
    while (!(stoppedAt = engine.stoppedAt(play)))
    {
        std::this_thread::sleep_for(1ms);
    }
    takeInput();
    while (!engine.quit())
    {
        std::this_thread::sleep_for(1ms);
    }
    thread.join();

    std::vector<Message> arrivals;
    {
        const std::lock_guard lock{mutex};
        arrivals = received;
    }
    const auto status = engine.status();
    const auto result = match(sink, arrivals);

    std::cout << "\nSent " << sink.sent.size() << " messages; received " << result.latency.size()
              << " of them. Lost " << result.lost << ", out of order " << result.outOfOrder
              << ", unexpected " << result.unexpected << ".\n";
    if (outputs.failedSends() > 0)
    {
        std::cout << outputs.failedSends() << " messages couldn't be sent.\n";
    }
    if (result.latency.empty())
    {
        std::cout << "Nothing came back: check the cable and the ports.\n";
        return 1;
    }

    const auto latency = summarize(result.latency);
    std::cout << '\n';
    printSummary("Latency (due to back)", latency);
    std::cout << "  " << std::left << std::setw(22) << "Jitter" << std::right << "p99 - median "
              << milliseconds(latency.p99 - latency.median) << "   standard deviation "
              << milliseconds(latency.deviation) << '\n';
    printSummary("After the send call", summarize(result.afterSend));
    std::cout << '\n';
    printTiming("Send lateness", status.lateness);
    printTiming("  waking", status.wakeLateness);
    printTiming("  sending", status.sendTime);
    if (!stream)
    {
        return result.lost == 0 ? 0 : 1;
    }

    // The recording, against what was sent.
    const auto recorded = recorder->finish(*stoppedAt);
    const auto sentNotes = stream->song.track(stream->sent)->events.size();
    const auto origin = sink.due.front() - plan->events.front().time;
    const auto check = checkRecording(*stream, recorded, arrivals, origin);
    std::cout << "\nRecorded " << check.late.size() << " of " << sentNotes << " notes. Missing "
              << check.missing << ", extra " << check.extra << ".\n";
    if (status.inputDropped + status.inputUnreported > 0)
    {
        std::cout << "Input: " << status.inputDropped << " messages dropped and "
                  << status.inputUnreported << " not passed back for recording.\n";
    }
    if (!check.late.empty())
    {
        std::cout << '\n';
        printSummary("Late (recorded - sent)", summarize(check.late));
        printTicks("  in ticks", check.lateTicks);
        printSummary("Longer (recorded - sent)", summarize(check.longer));
        std::cout << "  " << std::left << std::setw(22) << "Off arrival time" << std::right
                  << check.offArrival << " recorded at a tick other than the one they arrived at\n";
    }

    bool saved = true;
    if (!options.save.empty())
    {
        const auto written = core::exportSmfFile(recorded, options.save, core::SmfType::Type1);
        for (const auto& warning : written.warnings)
        {
            std::cout << "Warning: " << warning << '\n';
        }
        saved = written.error.empty();
        if (saved)
        {
            std::cout << "\nSaved " << options.save << ".\n";
        }
        else
        {
            std::cerr << "modal-loopback: " << written.error << '\n';
        }
    }
    const bool clean =
        result.lost == 0 && check.missing == 0 && check.extra == 0 && check.offArrival == 0;
    return clean && saved ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const auto parsed = parseOptions(argc, argv);
    if (const auto* error = std::get_if<std::string>(&parsed))
    {
        std::cerr << "modal-loopback: " << *error << "\n\n" << kUsage;
        return 2;
    }
    const auto& options = std::get<Options>(parsed);
    if (options.help)
    {
        std::cout << kUsage;
        return 0;
    }
    return run(options);
}
