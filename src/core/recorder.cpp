#include <modal/core/recorder.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::uint8_t kPedalDown = 64;                    // values from this up hold a pedal down
constexpr std::array<std::uint8_t, 3> kPedals{64, 66, 67}; // sustain, sostenuto, soft

std::optional<std::size_t> pedalIndex(std::uint8_t controller)
{
    const auto it = std::ranges::find(kPedals, controller);
    if (it == kPedals.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - kPedals.begin());
}

// The last value of each pedal, if any, among events up to a point.
class PedalState
{
public:
    void apply(const Event& event)
    {
        if (const auto* cc = std::get_if<ControlChange>(&event.data))
        {
            if (const auto index = pedalIndex(cc->controller))
            {
                values_[*index] = cc->value;
            }
        }
    }

    // The pedals held down, as controller and value.
    std::vector<ControlChange> down() const
    {
        std::vector<ControlChange> result;
        for (std::size_t i = 0; i < kPedals.size(); ++i)
        {
            if (values_[i] && *values_[i] >= kPedalDown)
            {
                result.push_back({kPedals[i], *values_[i]});
            }
        }
        return result;
    }

private:
    std::array<std::optional<std::uint8_t>, kPedals.size()> values_{};
};

Tick endOf(const Event& event)
{
    return event.tick + std::get<Note>(event.data).duration;
}

void setEnd(Event& event, Tick end)
{
    std::get<Note>(event.data).duration = end - event.tick;
}

// Keeps notes of one pitch from overlapping where a take note meets an old one: whichever
// starts first ends where the other starts, and at the same tick the take's note wins. Old
// notes that overlap each other are left as they are.
void resolveOverlaps(std::vector<Event>& old, std::vector<Event>& take)
{
    std::map<std::uint8_t, std::vector<std::size_t>> oldByPitch;
    for (std::size_t i = 0; i < old.size(); ++i)
    {
        if (const auto* note = std::get_if<Note>(&old[i].data))
        {
            oldByPitch[note->pitch].push_back(i);
        }
    }
    std::vector<bool> removed(old.size(), false);
    for (auto& event : take)
    {
        const auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const auto found = oldByPitch.find(note->pitch);
        if (found == oldByPitch.end())
        {
            continue;
        }
        for (const auto i : found->second) // in tick order
        {
            auto& other = old[i];
            if (removed[i])
            {
                continue;
            }
            if (other.tick == event.tick)
            {
                removed[i] = true;
            }
            else if (other.tick < event.tick && event.tick < endOf(other))
            {
                setEnd(other, event.tick);
            }
            else if (event.tick < other.tick && other.tick < endOf(event))
            {
                setEnd(event, other.tick);
            }
        }
    }
    std::vector<Event> kept;
    for (std::size_t i = 0; i < old.size(); ++i)
    {
        if (!removed[i])
        {
            kept.push_back(std::move(old[i]));
        }
    }
    old = std::move(kept);
}

} // namespace

Recorder::Recorder(Song song, std::uint64_t play, Tick start, RecordMode mode)
    : song_(std::move(song))
    , play_(play)
    , start_(start)
    , mode_(mode)
{
    for (const auto& track : song_.tracks())
    {
        if (track->arm)
        {
            takes_.push_back({track->id(), track->timeOffset, {}, {}, {}});
        }
    }
}

void Recorder::receive(const StampedInput& input)
{
    if (play_ == 0 || input.play != play_)
    {
        return;
    }
    const std::span<const std::uint8_t> bytes{input.chunk.bytes.data(), input.chunk.size};
    const auto partial = std::ranges::find(partial_, input.input, &PartialSysEx::input);

    if (input.first)
    {
        if (partial != partial_.end())
        {
            partial_.erase(partial); // cut short on the way back
        }
        if (input.chunk.more)
        {
            partial_.push_back({input.input, input.songTime, {bytes.begin(), bytes.end()}});
            return;
        }
        receiveMessage(input.input, bytes, input.songTime);
        return;
    }

    if (partial == partial_.end())
    {
        return; // the rest of a SysEx whose start was missed
    }
    partial->bytes.insert(partial->bytes.end(), bytes.begin(), bytes.end());
    if (!input.chunk.more)
    {
        const auto whole = std::move(*partial);
        partial_.erase(partial);
        receiveMessage(whole.input, whole.bytes, whole.songTime);
    }
}

void Recorder::receiveMessage(InputId input, std::span<const std::uint8_t> message,
                              nanoseconds songTime)
{
    if (message.empty())
    {
        return;
    }
    const auto status = static_cast<std::uint8_t>(message[0] & 0xF0);
    const auto inChannel = static_cast<std::uint8_t>(message[0] & 0x0F);
    const auto a = message.size() > 1 ? message[1] : std::uint8_t{0};
    const auto b = message.size() > 2 ? message[2] : std::uint8_t{0};

    std::optional<EventData> data;
    if (message[0] == 0xF0)
    {
        data = makeSysEx({message.begin(), message.end()});
        if (!isValid(Event{0, *data}))
        {
            return;
        }
    }
    else if (status == 0xA0)
    {
        data = PolyPressure{a, b};
    }
    else if (status == 0xB0)
    {
        data = ControlChange{a, b};
    }
    else if (status == 0xC0)
    {
        data = ProgramChange{a};
    }
    else if (status == 0xD0)
    {
        data = ChannelPressure{a};
    }
    else if (status == 0xE0)
    {
        data = PitchBend{static_cast<std::int16_t>((b << 7 | a) - 8192)};
    }
    else if (status != 0x80 && status != 0x90)
    {
        return;
    }

    for (auto& take : takes_)
    {
        const Tick tick = song_.tempo().toTickExtended(songTime - take.timeOffset);
        if (status == 0x90 && b > 0)
        {
            noteOn(take, input, inChannel, a, b, tick);
        }
        else if (status == 0x80 || status == 0x90)
        {
            noteOff(take, input, inChannel, a, status == 0x80 ? b : std::uint8_t{64}, tick);
        }
        else
        {
            take.events.push_back({Event{tick, *data}});
        }
    }
}

void Recorder::noteOn(TrackTake& take, InputId input, std::uint8_t inChannel, std::uint8_t pitch,
                      std::uint8_t velocity, Tick tick)
{
    if (const auto latest = take.latestNote[pitch])
    {
        auto& earlier = take.events[*latest];
        if (earlier.event.tick == tick)
        {
            earlier.removed = true;
        }
        else if (!earlier.ended || endOf(earlier.event) > tick)
        {
            setEnd(earlier.event, tick);
        }
        earlier.ended = true;
        std::erase_if(take.held, [&](const HeldNote& held) { return held.index == *latest; });
    }
    take.latestNote[pitch] = take.events.size();
    take.held.push_back({input, inChannel, pitch, take.events.size()});
    take.events.push_back({Event{tick, Note{pitch, velocity, 0}}, false});
}

void Recorder::noteOff(TrackTake& take, InputId input, std::uint8_t inChannel, std::uint8_t pitch,
                       std::uint8_t releaseVelocity, Tick tick)
{
    const auto held = std::ranges::find_if(
        take.held, [&](const HeldNote& note)
        { return note.input == input && note.inChannel == inChannel && note.pitch == pitch; });
    if (held == take.held.end())
    {
        return;
    }
    auto& note = take.events[held->index];
    setEnd(note.event, std::max(tick, note.event.tick + 1));
    std::get<Note>(note.event.data).releaseVelocity = releaseVelocity;
    note.ended = true;
    take.held.erase(held);
}

Song Recorder::finish(Tick stop) const
{
    Song song = song_;
    if (stop <= start_)
    {
        return song;
    }
    for (const auto& take : takes_)
    {
        song.updateTrack(take.track, [&](Track& track) { track = applied(track, take, stop); });
    }
    return song;
}

Track Recorder::applied(const Track& track, const TrackTake& take, Tick stop) const
{
    // The take, kept to [start, stop), with what was held at the record point starting there.
    PedalState takeAtStart;
    PedalState takeAtStop;
    std::vector<Event> recorded;
    for (const auto& [event, ended, removed] : take.events)
    {
        if (removed || event.tick >= stop)
        {
            continue;
        }
        if (!event.isNote())
        {
            if (event.tick < start_)
            {
                takeAtStart.apply(event);
                takeAtStop.apply(event);
            }
            else
            {
                takeAtStop.apply(event);
                recorded.push_back(event);
            }
            continue;
        }
        const Tick end = ended ? std::min(endOf(event), stop) : stop;
        if (end <= start_)
        {
            continue;
        }
        Event note = event;
        note.tick = std::max(event.tick, start_);
        setEnd(note, end);
        recorded.push_back(std::move(note));
    }
    std::vector<Event> heldAtStart;
    for (const auto& pedal : takeAtStart.down())
    {
        heldAtStart.push_back({start_, pedal});
    }
    std::vector<Event> releasedAtStop;
    for (const auto& pedal : takeAtStop.down())
    {
        releasedAtStop.push_back({stop, ControlChange{pedal.controller, 0}});
    }

    // What stays of the track, with its pedals patched at the edges of what was replaced.
    PedalState oldAtStart;
    PedalState oldAtStop;
    std::vector<Event> old;
    for (const auto& event : track.events.all())
    {
        if (event.tick < start_)
        {
            oldAtStart.apply(event);
        }
        if (event.tick <= stop)
        {
            oldAtStop.apply(event);
        }
        if (mode_ == RecordMode::Overdub || event.tick >= stop)
        {
            old.push_back(event);
        }
        else if (event.tick < start_)
        {
            old.push_back(event);
            if (event.isNote() && endOf(event) > start_)
            {
                setEnd(old.back(), start_);
            }
        }
    }
    std::vector<Event> upAtStart;
    std::vector<Event> downAtStop;
    for (const auto& pedal : oldAtStart.down())
    {
        if (mode_ == RecordMode::Replace)
        {
            upAtStart.push_back({start_, ControlChange{pedal.controller, 0}});
        }
    }
    for (const auto& pedal : oldAtStop.down())
    {
        const bool released = std::ranges::any_of(
            releasedAtStop, [&](const Event& event)
            { return std::get<ControlChange>(event.data).controller == pedal.controller; });
        if (mode_ == RecordMode::Replace || released)
        {
            downAtStop.push_back({stop, pedal});
        }
    }

    resolveOverlaps(old, recorded);

    // At each tick, non-note events keep this order, and notes follow them.
    std::vector<Event> all;
    for (auto* part : {&old, &upAtStart, &heldAtStart, &recorded, &releasedAtStop, &downAtStop})
    {
        all.insert(all.end(), part->begin(), part->end());
    }
    std::ranges::stable_sort(all, {}, &Event::tick);

    Track result = track;
    result.events = {};
    for (auto& event : all)
    {
        result.events.insert(std::move(event));
    }
    return result;
}

} // namespace modal::core
