#include <modal/core/scheduler.hpp>

#include <algorithm>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kControlChange = 0xB0;

constexpr std::uint8_t kSustain = 64;
constexpr std::uint8_t kResetAllControllers = 121;
constexpr std::uint8_t kAllNotesOff = 123;
constexpr std::uint8_t kChannels = 16;

// The time in the new tempo of the tick being played at a time in the old one, keeping the
// time since that tick as far as it fits before the next. Before tick 0 there's no tempo.
nanoseconds remap(nanoseconds time, const TempoMap& from, const TempoMap& to)
{
    if (time < nanoseconds{0})
    {
        return time;
    }
    const Tick tick = from.toTick(time);
    const nanoseconds since = time - from.toTime(tick);
    return std::min(to.toTime(tick) + since, to.toTime(tick + 1) - nanoseconds{1});
}

} // namespace

Scheduler::Scheduler(MessageSink& sink, StopOptions options)
    : sink_(sink)
    , options_(options)
{
    sounding_.reserve(kMaxSoundingNotes);
}

void Scheduler::setPlan(const PlaybackPlan& plan, nanoseconds now) noexcept
{
    rememberDestinations(plan);
    if (!playing_)
    {
        plan_ = &plan;
        return;
    }

    sendDue(now);
    const nanoseconds position = remap(now - origin_, plan_->tempo, plan.tempo);
    origin_ = now - position;
    plan_ = &plan;

    for (std::size_t i = 0; i < sounding_.size();)
    {
        auto& note = sounding_[i];
        const auto* track = plan.track(note.track);
        if (track == nullptr)
        {
            noteOff(i, now); // moves the last note into i
            continue;
        }
        note.end = plan.tempo.toTime(note.endTick) + track->timeOffset;
        ++i;
    }

    // Everything at or before the position has been sent, from one plan or the other.
    const auto it = std::ranges::upper_bound(plan.events, position, {}, &PlannedEvent::time);
    next_ = static_cast<std::size_t>(it - plan.events.begin());
}

void Scheduler::play(Tick start, std::span<const PlannedEvent> chased, nanoseconds now) noexcept
{
    if (plan_ == nullptr)
    {
        return;
    }
    if (playing_)
    {
        endAllNotes(now);
    }
    const nanoseconds startTime = plan_->tempo.toTime(start) - plan_->leadIn;
    playing_ = true;
    startTick_ = start;
    origin_ = now - startTime;
    next_ = plan_->firstAtOrAfter(startTime);
    chased_ = chased;
}

void Scheduler::stop(nanoseconds now) noexcept
{
    if (!playing_)
    {
        return;
    }
    endAllNotes(now);
    playing_ = false;
    chased_ = {};

    if (!options_.allNotesOff && !options_.resetSustain)
    {
        return;
    }
    const auto& tracks = plan_->tracks;
    for (auto it = tracks.begin(); it != tracks.end(); ++it)
    {
        const bool seen = std::any_of(
            tracks.begin(), it, [&](const PlannedTrack& earlier)
            { return earlier.destination == it->destination && earlier.channel == it->channel; });
        if (seen)
        {
            continue;
        }
        if (options_.resetSustain)
        {
            sendControl(it->destination, it->channel, kSustain, 0, now);
        }
        if (options_.allNotesOff)
        {
            sendControl(it->destination, it->channel, kAllNotesOff, 0, now);
        }
    }
}

void Scheduler::panic(nanoseconds now) noexcept
{
    endAllNotes(now);
    playing_ = false;
    chased_ = {};
    for (std::size_t d = 0; d < destinationCount_; ++d)
    {
        for (std::uint8_t channel = 0; channel < kChannels; ++channel)
        {
            sendControl(destinations_[d], channel, kAllNotesOff, 0, now);
            sendControl(destinations_[d], channel, kResetAllControllers, 0, now);
        }
    }
}

void Scheduler::sendDue(nanoseconds now) noexcept
{
    if (!playing_)
    {
        return;
    }
    for (const auto& event : chased_)
    {
        send(event);
    }
    chased_ = {};

    const nanoseconds position = now - origin_;
    const auto& events = plan_->events;
    for (;;)
    {
        const auto off = nextNoteOff();
        const bool offDue = off && sounding_[*off].end <= position;
        const bool eventDue = next_ < events.size() && events[next_].time <= position;
        if (offDue && (!eventDue || sounding_[*off].end <= events[next_].time))
        {
            noteOff(*off, origin_ + sounding_[*off].end);
        }
        else if (eventDue)
        {
            const auto& event = events[next_++];
            if (event.tick >= startTick_)
            {
                send(event);
            }
        }
        else
        {
            break;
        }
    }
}

std::optional<nanoseconds> Scheduler::nextWakeTime() const noexcept
{
    if (!playing_)
    {
        return std::nullopt;
    }
    if (!chased_.empty())
    {
        return origin_ + chased_.front().time;
    }
    std::optional<nanoseconds> next;
    if (next_ < plan_->events.size())
    {
        next = plan_->events[next_].time;
    }
    if (const auto off = nextNoteOff())
    {
        next = next ? std::min(*next, sounding_[*off].end) : sounding_[*off].end;
    }
    if (!next)
    {
        return std::nullopt;
    }
    return origin_ + *next;
}

bool Scheduler::finished() const noexcept
{
    return playing_ && chased_.empty() && next_ == plan_->events.size() && sounding_.empty();
}

std::optional<Tick> Scheduler::position(nanoseconds now) const noexcept
{
    if (!playing_)
    {
        return std::nullopt;
    }
    return plan_->tempo.toTick(std::max(now - origin_, nanoseconds{0}));
}

std::optional<Tick> Scheduler::tickAt(nanoseconds time) const noexcept
{
    if (!playing_)
    {
        return std::nullopt;
    }
    return plan_->tempo.toTickExtended(time - origin_);
}

std::optional<nanoseconds> Scheduler::songTimeAt(nanoseconds time) const noexcept
{
    if (!playing_)
    {
        return std::nullopt;
    }
    return time - origin_;
}

void Scheduler::send(const PlannedEvent& event) noexcept
{
    const nanoseconds due = origin_ + event.time;
    if (event.sysex)
    {
        sink_.send(event.destination, *event.sysex, due);
    }
    else if (event.isNoteOn())
    {
        noteOn(event, due);
    }
    else
    {
        sink_.send(event.destination, event.message.view(), due);
    }
}

void Scheduler::noteOn(const PlannedEvent& event, nanoseconds due) noexcept
{
    const auto channel = static_cast<std::uint8_t>(event.message.bytes[0] & 0x0F);
    const auto pitch = event.message.bytes[1];
    const auto it = std::ranges::find_if(sounding_,
                                         [&](const SoundingNote& note)
                                         {
                                             return note.destination == event.destination &&
                                                    note.channel == channel && note.pitch == pitch;
                                         });

    if (it == sounding_.end())
    {
        if (sounding_.size() == kMaxSoundingNotes)
        {
            ++droppedNotes_;
            return;
        }
        sounding_.push_back({event.destination, channel, pitch, event.releaseVelocity, event.track,
                             event.endTick, event.noteOffTime});
    }
    else
    {
        // Retrigger, and end when the last of the overlapping notes ends.
        const std::uint8_t off[] = {static_cast<std::uint8_t>(kNoteOff | channel), pitch,
                                    it->releaseVelocity};
        sink_.send(event.destination, off, due);
        if (event.noteOffTime >= it->end)
        {
            it->releaseVelocity = event.releaseVelocity;
            it->track = event.track;
            it->endTick = event.endTick;
            it->end = event.noteOffTime;
        }
    }
    sink_.send(event.destination, event.message.view(), due);
}

void Scheduler::noteOff(std::size_t index, nanoseconds due) noexcept
{
    const auto& note = sounding_[index];
    const std::uint8_t off[] = {static_cast<std::uint8_t>(kNoteOff | note.channel), note.pitch,
                                note.releaseVelocity};
    sink_.send(note.destination, off, due);
    sounding_[index] = sounding_.back();
    sounding_.pop_back();
}

void Scheduler::endAllNotes(nanoseconds now) noexcept
{
    while (!sounding_.empty())
    {
        noteOff(sounding_.size() - 1, now);
    }
}

void Scheduler::sendControl(DestinationId destination, std::uint8_t channel,
                            std::uint8_t controller, std::uint8_t value, nanoseconds due) noexcept
{
    const std::uint8_t message[] = {static_cast<std::uint8_t>(kControlChange | channel), controller,
                                    value};
    sink_.send(destination, message, due);
}

std::optional<std::size_t> Scheduler::nextNoteOff() const noexcept
{
    if (sounding_.empty())
    {
        return std::nullopt;
    }
    const auto it = std::ranges::min_element(sounding_, {}, &SoundingNote::end);
    return static_cast<std::size_t>(it - sounding_.begin());
}

void Scheduler::rememberDestinations(const PlaybackPlan& plan) noexcept
{
    const auto remember = [&](DestinationId destination)
    {
        const auto known = std::span{destinations_}.first(destinationCount_);
        if (std::ranges::find(known, destination) == known.end() &&
            destinationCount_ < kMaxDestinations)
        {
            destinations_[destinationCount_++] = destination;
        }
    };
    for (const auto& track : plan.tracks)
    {
        remember(track.destination);
    }
    for (const auto& target : plan.thru) // panic reaches what thru has sent to, too
    {
        remember(target.destination);
    }
}

} // namespace modal::core
