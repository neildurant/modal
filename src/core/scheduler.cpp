// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/scheduler.hpp>

#include <algorithm>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kControlChange = 0xB0;

constexpr std::uint8_t kSustain = 64;
constexpr std::uint8_t kSostenuto = 66;
constexpr std::uint8_t kSoft = 67;
constexpr std::uint8_t kPedalDown = 64; // values from this up hold a pedal down
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
    pedals_.reserve(kMaxHeldPedals);
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

    const auto& metronome = plan.metronome;
    if (click_ && click_->endTick)
    {
        if (metronome && metronome->destination == click_->destination &&
            metronome->channel == click_->channel)
        {
            click_->end = origin_ + plan.tempo.toTime(*click_->endTick);
        }
        else
        {
            endClick(now);
        }
    }
    nextClick_.reset();
    if (metronome)
    {
        // The clicks at or before the position have been sent.
        const Tick from = position < nanoseconds{0} ? 0 : plan.tempo.toTick(position) + 1;
        nextClick_ = metronome->meter.beatAtOrAfter(std::max(from, startTick_));
    }

    // Everything at or before the position has been sent, from one plan or the other.
    const auto it = std::ranges::upper_bound(plan.events, position, {}, &PlannedEvent::time);
    next_ = static_cast<std::size_t>(it - plan.events.begin());
}

void Scheduler::play(Tick start, std::span<const PlannedEvent> chased, nanoseconds now,
                     const PlannedCountIn* countIn) noexcept
{
    if (plan_ == nullptr)
    {
        return;
    }
    if (playing_)
    {
        endAllNotes(now);
        releasePedals(now);
    }
    const nanoseconds lead =
        std::max(plan_->leadIn, countIn != nullptr ? countIn->length : nanoseconds{0});
    const nanoseconds startTime = plan_->tempo.toTime(start) - lead;
    playing_ = true;
    startTick_ = start;
    origin_ = now - startTime;
    countIn_ = countIn;
    nextCountIn_ = 0;
    if (countIn != nullptr)
    {
        rememberDestination(countIn->destination);
    }
    recordPoint_ = origin_ + plan_->tempo.toTime(start);
    next_ = plan_->firstAtOrAfter(startTime);
    chased_ = chased;
    nextClick_.reset();
    if (plan_->metronome)
    {
        nextClick_ = plan_->metronome->meter.beatAtOrAfter(start);
    }
}

void Scheduler::stop(nanoseconds now) noexcept
{
    if (!playing_)
    {
        return;
    }
    endAllNotes(now);
    releasePedals(now);
    playing_ = false;
    chased_ = {};
    nextClick_.reset();
    countIn_ = nullptr;

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
    pedals_.clear(); // Reset All Controllers lets them up
    playing_ = false;
    chased_ = {};
    nextClick_.reset();
    countIn_ = nullptr;
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
        // The earliest thing due; at equal times, the first considered.
        enum class Due : std::uint8_t
        {
            Nothing,
            NoteOff,
            ClickOff,
            Click,
            CountInClick,
            Event,
        };
        Due due = Due::Nothing;
        nanoseconds dueTime{0};
        const auto consider = [&](Due what, nanoseconds time)
        {
            if (time <= position && (due == Due::Nothing || time < dueTime))
            {
                due = what;
                dueTime = time;
            }
        };
        const auto off = nextNoteOff();
        if (off)
        {
            consider(Due::NoteOff, sounding_[*off].end);
        }
        if (click_)
        {
            consider(Due::ClickOff, click_->end - origin_);
        }
        const auto click = clickDue();
        if (click)
        {
            consider(Due::Click, plan_->tempo.toTime(*click));
        }
        if (const auto countIn = countInDue())
        {
            consider(Due::CountInClick, *countIn);
        }
        if (next_ < events.size())
        {
            consider(Due::Event, events[next_].time);
        }

        switch (due)
        {
        case Due::Nothing:
            return;
        case Due::NoteOff:
            noteOff(*off, origin_ + dueTime);
            break;
        case Due::ClickOff:
            endClick(origin_ + dueTime);
            break;
        case Due::Click:
            sendClick(*click);
            break;
        case Due::CountInClick:
            sendCountInClick();
            break;
        case Due::Event:
        {
            const auto& event = events[next_++];
            if (event.tick >= startTick_)
            {
                send(event);
            }
            break;
        }
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
    const auto earliest = [&](nanoseconds time) { next = next ? std::min(*next, time) : time; };
    if (const auto off = nextNoteOff())
    {
        earliest(sounding_[*off].end);
    }
    if (click_)
    {
        earliest(click_->end - origin_);
    }
    if (const auto countIn = countInDue())
    {
        earliest(*countIn);
    }
    if (const auto click = clickDue())
    {
        earliest(plan_->tempo.toTime(*click));
    }
    if (!next)
    {
        return std::nullopt;
    }
    return origin_ + *next;
}

bool Scheduler::finished() const noexcept
{
    return playing_ && songDone() && !clickDue() && !click_ && !countInDue();
}

std::optional<nanoseconds> Scheduler::countInDue() const noexcept
{
    if (countIn_ == nullptr || nextCountIn_ == countIn_->clicks.size())
    {
        return std::nullopt;
    }
    return recordPoint_ - countIn_->clicks[nextCountIn_].before - origin_;
}

bool Scheduler::songDone() const noexcept
{
    return chased_.empty() && next_ == plan_->events.size() && sounding_.empty();
}

std::optional<Tick> Scheduler::clickDue() const noexcept
{
    if (!nextClick_ || (!plan_->metronome->pastEnd && songDone()))
    {
        return std::nullopt;
    }
    return nextClick_;
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
        notePedal(event);
        sink_.send(event.destination, event.message.view(), due);
    }
}

void Scheduler::notePedal(const PlannedEvent& event) noexcept
{
    const auto& bytes = event.message.bytes;
    const std::uint8_t controller = bytes[1];
    if (event.message.size != 3 || (bytes[0] & 0xF0) != kControlChange ||
        (controller != kSustain && controller != kSostenuto && controller != kSoft))
    {
        return;
    }
    const HeldPedal pedal{event.destination, static_cast<std::uint8_t>(bytes[0] & 0x0F),
                          controller};
    const auto it = std::ranges::find(pedals_, pedal);
    if (bytes[2] < kPedalDown)
    {
        if (it != pedals_.end())
        {
            pedals_.erase(it); // keeps the order they went down in, for pedal-ups
        }
    }
    else if (it == pedals_.end() && pedals_.size() < kMaxHeldPedals)
    {
        pedals_.push_back(pedal);
    }
}

void Scheduler::releasePedals(nanoseconds now) noexcept
{
    for (const auto& pedal : pedals_)
    {
        sendControl(pedal.destination, pedal.channel, pedal.controller, 0, now);
    }
    pedals_.clear();
}

void Scheduler::sendClick(Tick tick) noexcept
{
    const auto& metronome = *plan_->metronome;
    const nanoseconds due = origin_ + plan_->tempo.toTime(tick);
    if (click_)
    {
        endClick(due);
    }
    const bool downbeat = metronome.meter.toBarBeatTick(tick).beat == 1;
    const auto pitch = downbeat ? metronome.downbeatNote : metronome.beatNote;
    const auto velocity = downbeat ? metronome.downbeatVelocity : metronome.beatVelocity;
    const std::uint8_t on[] = {static_cast<std::uint8_t>(kNoteOn | metronome.channel), pitch,
                               velocity};
    sink_.send(metronome.destination, on, due);

    const Tick next = metronome.meter.beatAtOrAfter(tick + 1);
    const Tick endTick = tick + std::min(metronome.length, next - tick);
    click_ = Click{metronome.destination, metronome.channel, pitch, endTick,
                   origin_ + plan_->tempo.toTime(endTick)};
    nextClick_ = next;
}

void Scheduler::sendCountInClick() noexcept
{
    const auto& countIn = *countIn_;
    const auto& click = countIn.clicks[nextCountIn_++];
    const nanoseconds due = recordPoint_ - click.before;
    if (click_)
    {
        endClick(due);
    }
    const auto pitch = click.accent ? countIn.accentNote : countIn.beatNote;
    const auto velocity = click.accent ? countIn.accentVelocity : countIn.beatVelocity;
    const std::uint8_t on[] = {static_cast<std::uint8_t>(kNoteOn | countIn.channel), pitch,
                               velocity};
    sink_.send(countIn.destination, on, due);
    click_ =
        Click{countIn.destination, countIn.channel, pitch, std::nullopt, due + countIn.clickLength};
}

void Scheduler::endClick(nanoseconds due) noexcept
{
    const std::uint8_t off[] = {static_cast<std::uint8_t>(kNoteOff | click_->channel),
                                click_->pitch, 64};
    sink_.send(click_->destination, off, due);
    click_.reset();
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
    if (click_)
    {
        endClick(now);
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
    for (const auto& track : plan.tracks)
    {
        rememberDestination(track.destination);
    }
    for (const auto& target : plan.thru) // panic reaches what thru has sent to, too
    {
        rememberDestination(target.destination);
    }
    if (plan.metronome)
    {
        rememberDestination(plan.metronome->destination);
    }
}

void Scheduler::rememberDestination(DestinationId destination) noexcept
{
    const auto known = std::span{destinations_}.first(destinationCount_);
    if (std::ranges::find(known, destination) == known.end() &&
        destinationCount_ < kMaxDestinations)
    {
        destinations_[destinationCount_++] = destination;
    }
}

} // namespace modal::core
