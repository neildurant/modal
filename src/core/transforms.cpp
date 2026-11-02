// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/transforms.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace modal::core
{

namespace
{

struct Entry
{
    Event event;
    bool changed = false;
};

Tick endOf(const Event& event)
{
    return event.tick + std::max<Tick>(std::get<Note>(event.data).duration, 1);
}

// The events with those taken replaced by their changed selves, in the same order; refused
// where notes of a pitch would overlap and one of them was changed. With trim, a note run into
// the next of its pitch ends where that starts.
Transformed finish(const EventList& events, std::span<const std::size_t> which,
                   std::span<const Event> changed, bool trim)
{
    std::vector<Entry> all;
    all.reserve(events.size());
    std::size_t taken = 0;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (taken < which.size() && which[taken] == i)
        {
            all.push_back({changed[taken], changed[taken] != events[i]});
            ++taken;
        }
        else
        {
            all.push_back({events[i], false});
        }
    }

    std::vector<std::size_t> notes;
    for (std::size_t k = 0; k < all.size(); ++k)
    {
        if (all[k].event.isNote())
        {
            notes.push_back(k);
        }
    }
    const auto pitch = [&](std::size_t k) { return std::get<Note>(all[k].event.data).pitch; };
    std::ranges::stable_sort(notes,
                             [&](std::size_t a, std::size_t b)
                             {
                                 return std::pair{pitch(a), all[a].event.tick} <
                                        std::pair{pitch(b), all[b].event.tick};
                             });
    for (std::size_t n = 1; n < notes.size(); ++n)
    {
        auto& a = all[notes[n - 1]];
        auto& b = all[notes[n]];
        if (pitch(notes[n - 1]) != pitch(notes[n]) || !(a.changed || b.changed) ||
            endOf(a.event) <= b.event.tick)
        {
            continue;
        }
        if (trim && a.event.tick < b.event.tick)
        {
            std::get<Note>(a.event.data).duration = b.event.tick - a.event.tick;
            a.changed = true;
            continue;
        }
        const auto& [mine, theirs] = b.changed ? std::pair{b, a} : std::pair{a, b};
        return Refusal{Refusal::Reason::Overlap, mine.event, theirs.event};
    }

    std::ranges::stable_sort(all, {}, [](const Entry& e) { return e.event.tick; });
    EventList result;
    for (auto& entry : all)
    {
        result.insert(std::move(entry.event));
    }
    return result;
}

std::vector<Event> takenEvents(const EventList& events, std::span<const std::size_t> which)
{
    std::vector<Event> taken;
    taken.reserve(which.size());
    for (const auto i : which)
    {
        taken.push_back(events[i]);
    }
    return taken;
}

std::uint8_t clampedVelocity(std::int64_t velocity)
{
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(velocity, 1, 127));
}

std::uint8_t limited(double velocity, VelocityLimits limits)
{
    const auto floor = std::clamp(limits.floor, 1, 127);
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(std::llround(velocity), floor, 127));
}

// The soft knee towards the top, as a curve from a velocity with no limit to one under 127,
// the same below the knee, and its inverse; the bottom's mirrors it above the floor.
double kneeTop(double u)
{
    const double start = 127.0 - kVelocityKnee;
    return u <= start ? u : start + kVelocityKnee * (1.0 - std::exp(-(u - start) / kVelocityKnee));
}

double unkneeTop(double v)
{
    const double start = 127.0 - kVelocityKnee;
    v = std::min(v, 126.5); // 127 itself as just under, to come back from
    return v <= start ? v : start - kVelocityKnee * std::log(1.0 - (v - start) / kVelocityKnee);
}

double kneeBottom(double u, double floor)
{
    const double start = floor + kVelocityKnee;
    return u >= start ? u : start - kVelocityKnee * (1.0 - std::exp(-(start - u) / kVelocityKnee));
}

double unkneeBottom(double v, double floor)
{
    const double start = floor + kVelocityKnee;
    v = std::max(v, floor + 0.5);
    return v >= start ? v : start + kVelocityKnee * std::log(1.0 - (start - v) / kVelocityKnee);
}

template <typename Change>
Transformed changeNotes(const EventList& events, std::span<const std::size_t> which, Change change)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        if (auto* note = std::get_if<Note>(&event.data))
        {
            note->velocity = change(*note, event.tick);
        }
    }
    return finish(events, which, changed, false);
}

std::optional<int> number(std::string_view text)
{
    int value = 0;
    const auto* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    if (text.empty() || error != std::errc{} || end != last)
    {
        return std::nullopt;
    }
    return value;
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

Transformed transpose(const EventList& events, std::span<const std::size_t> which, int semitones)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const int to = note->pitch + semitones;
        if (to > 127 || to < 0)
        {
            return Refusal{to > 127 ? Refusal::Reason::AboveHighest : Refusal::Reason::BelowLowest,
                           event, std::nullopt};
        }
        note->pitch = static_cast<std::uint8_t>(to);
    }
    return finish(events, which, changed, false);
}

namespace
{

Transformed repitched(const EventList& events, std::span<const std::size_t> which,
                      const std::function<int(int)>& to)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const int pitch = to(note->pitch);
        if (pitch > 127 || pitch < 0)
        {
            return Refusal{pitch > 127 ? Refusal::Reason::AboveHighest
                                       : Refusal::Reason::BelowLowest,
                           event, std::nullopt};
        }
        note->pitch = static_cast<std::uint8_t>(pitch);
    }
    return finish(events, which, changed, false);
}

} // namespace

Transformed transposeInKey(const EventList& events, std::span<const std::size_t> which,
                           const Key& key, int degrees)
{
    return repitched(events, which,
                     [&](int pitch)
                     {
                         const auto at = key.place(pitch);
                         return key.pitchOf(at.degree + degrees) + at.above;
                     });
}

Transformed invertInKey(const EventList& events, std::span<const std::size_t> which, const Key& key,
                        int pivot)
{
    const auto about = key.place(pivot).degree;
    return repitched(events, which,
                     [&](int pitch)
                     {
                         const auto at = key.place(pitch);
                         return key.pitchOf(2 * about - at.degree) + at.above;
                     });
}

Transformed invert(const EventList& events, std::span<const std::size_t> which, int pivot)
{
    return repitched(events, which, [&](int pitch) { return 2 * pivot - pitch; });
}

Transformed retrograde(const EventList& events, std::span<const std::size_t> which)
{
    std::optional<Tick> first;
    std::optional<Tick> last;
    for (const auto i : which)
    {
        if (events[i].isNote())
        {
            first = std::min(first.value_or(events[i].tick), events[i].tick);
            last = std::max(last.value_or(endOf(events[i])), endOf(events[i]));
        }
    }
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        if (event.isNote())
        {
            event.tick = *first + *last - endOf(event);
        }
    }
    return finish(events, which, changed, false);
}

Transformed shift(const EventList& events, std::span<const std::size_t> which, Tick by, Tick end,
                  bool notesOnly)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        if (notesOnly && !event.isNote())
        {
            continue;
        }
        const auto was = event;
        event.tick += by;
        if (event.tick < 0 || event.tick >= end)
        {
            return Refusal{event.tick < 0 ? Refusal::Reason::BeforeStart : Refusal::Reason::PastEnd,
                           was, std::nullopt};
        }
    }
    return finish(events, which, changed, true);
}

bool isPedal(const Event& event)
{
    const auto* change = std::get_if<ControlChange>(&event.data);
    return change != nullptr &&
           (change->controller == 64 || change->controller == 66 || change->controller == 67);
}

bool hasPedals(const EventList& events, std::span<const std::size_t> which)
{
    return std::ranges::any_of(which, [&](std::size_t i) { return isPedal(events[i]); });
}

Transformed quantize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& nearest, bool pedals)
{
    auto changed = takenEvents(events, which);
    // Each note's start, and how far it moved, in tick order.
    std::vector<std::pair<Tick, Tick>> moves;
    for (auto& event : changed)
    {
        if (event.isNote())
        {
            const auto to = nearest(event.tick);
            moves.emplace_back(event.tick, to - event.tick);
            event.tick = to;
        }
    }
    std::ranges::sort(moves);
    if (pedals)
    {
        for (auto& event : changed)
        {
            if (!isPedal(event))
            {
                continue;
            }
            // The latest note at or before it.
            const auto after =
                std::ranges::upper_bound(moves, event.tick, {}, &std::pair<Tick, Tick>::first);
            if (after != moves.begin())
            {
                event.tick = std::max<Tick>(event.tick + std::prev(after)->second, 0);
            }
        }
        // Each pedal's events, the track's whole, keep their order.
        std::array<std::optional<Event>, 3> last; // as each would be
        std::size_t taken = 0;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            auto event = events[i];
            if (taken < which.size() && which[taken] == i)
            {
                event = changed[taken++];
            }
            if (!isPedal(event))
            {
                continue;
            }
            const auto controller = std::get<ControlChange>(event.data).controller;
            auto& before = last[controller == 64 ? 0 : controller == 66 ? 1 : 2];
            if (before && event.tick < before->tick)
            {
                const bool moved = event.tick != events[i].tick;
                return Refusal{Refusal::Reason::Order, moved ? event : *before,
                               moved ? *before : event};
            }
            before = event;
        }
    }
    return finish(events, which, changed, true);
}

Tick nearestGridLine(const MeterMap& meter, Tick tick, Tick step)
{
    step = std::max<Tick>(step, 1);
    const auto bar = meter.toBarBeatTick(tick).bar;
    const auto start = meter.barStart(bar);
    const auto next = meter.barStart(bar + 1);
    const auto before = start + (tick - start) / step * step;
    const auto after = std::min(before + step, next);
    return tick - before >= after - tick ? after : before;
}

std::optional<VelocityChange> parseVelocityChange(std::string_view text)
{
    while (!text.empty() && text.front() == ' ')
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.remove_suffix(1);
    }
    auto kind = VelocityChange::Kind::Set;
    int sign = 1;
    if (text.ends_with('%'))
    {
        kind = VelocityChange::Kind::Percent;
        text.remove_suffix(1);
    }
    else if (text.starts_with('+') || text.starts_with('-'))
    {
        kind = VelocityChange::Kind::Add;
        sign = text.front() == '-' ? -1 : 1;
        text.remove_prefix(1);
    }
    int amount = 0;
    const auto* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, amount);
    if (text.empty() || error != std::errc{} || end != last)
    {
        return std::nullopt;
    }
    const bool fits = kind == VelocityChange::Kind::Set   ? amount >= 1 && amount <= 127
                      : kind == VelocityChange::Kind::Add ? amount <= 126
                                                          : amount <= 1000;
    if (!fits)
    {
        return std::nullopt;
    }
    return VelocityChange{kind, sign * amount};
}

std::uint8_t eased(std::uint8_t velocity, double target, VelocityLimits limits)
{
    if (!limits.soft)
    {
        return limited(target, limits);
    }
    // The change made on the curve's far side, where there's no limit, and brought back.
    const double v = velocity;
    const double floor = std::clamp(limits.floor, 1, 127);
    const double by = target - v;
    if (by > 0)
    {
        return limited(kneeTop(unkneeTop(v) + by), limits);
    }
    if (by < 0)
    {
        return limited(kneeBottom(unkneeBottom(v, floor) + by, floor), limits);
    }
    return limited(v, limits);
}

std::uint8_t changedVelocity(std::uint8_t velocity, VelocityChange change, VelocityLimits limits)
{
    switch (change.kind)
    {
    case VelocityChange::Kind::Set:
        return limited(change.amount, limits);
    case VelocityChange::Kind::Add:
        return eased(velocity, static_cast<double>(velocity) + change.amount, limits);
    case VelocityChange::Kind::Percent:
        return eased(velocity, static_cast<double>(velocity) * change.amount / 100.0, limits);
    }
    return velocity;
}

Transformed changeVelocity(const EventList& events, std::span<const std::size_t> which,
                           VelocityChange change, VelocityLimits limits)
{
    return changeNotes(events, which, [&](const Note& note, Tick)
                       { return changedVelocity(note.velocity, change, limits); });
}

Transformed liftVelocity(const EventList& events, std::span<const std::size_t> which, int percent,
                         VelocityLimits limits)
{
    return changeNotes(events, which,
                       [&](const Note& note, Tick)
                       {
                           const double v = note.velocity;
                           return limited(v + (127.0 - v) * percent / 100.0, limits);
                       });
}

Transformed lowerVelocity(const EventList& events, std::span<const std::size_t> which, int percent,
                          VelocityLimits limits)
{
    const double floor = std::clamp(limits.floor, 1, 127);
    return changeNotes(events, which,
                       [&](const Note& note, Tick)
                       {
                           const double v = note.velocity;
                           return v <= floor ? limited(v, limits)
                                             : limited(v - (v - floor) * percent / 100.0, limits);
                       });
}

Transformed limitVelocity(const EventList& events, std::span<const std::size_t> which, int low,
                          int high, VelocityLimits limits)
{
    return changeNotes(events, which, [&](const Note& note, Tick)
                       { return limited(std::clamp<int>(note.velocity, low, high), limits); });
}

Transformed rampVelocity(const EventList& events, std::span<const std::size_t> which,
                         VelocityRamp ramp, VelocityLimits limits)
{
    // From the first note's start to the last's, a chord's notes alike.
    std::optional<Tick> first;
    std::optional<Tick> last;
    for (const auto i : which)
    {
        if (std::holds_alternative<Note>(events[i].data))
        {
            first = std::min(first.value_or(events[i].tick), events[i].tick);
            last = std::max(last.value_or(events[i].tick), events[i].tick);
        }
    }
    const auto span = first ? *last - *first : Tick{0};
    return changeNotes(events, which,
                       [&](const Note& note, Tick tick)
                       {
                           const double along = span > 0 ? double(tick - *first) / double(span) : 0;
                           const double amount = ramp.from + (ramp.to - ramp.from) * along;
                           return ramp.percent
                                      ? eased(note.velocity, note.velocity * amount / 100.0, limits)
                                      : limited(amount, limits);
                       });
}

Transformed velocityDynamics(const EventList& events, std::span<const std::size_t> which,
                             const VelocityDynamics& dynamics, VelocityLimits limits)
{
    using Kind = VelocityDynamics::Kind;
    using Centre = VelocityDynamics::Centre;
    std::vector<std::size_t> notes;
    double sum = 0;
    for (const auto i : which)
    {
        if (const auto* note = std::get_if<Note>(&events[i].data))
        {
            notes.push_back(i);
            sum += note->velocity;
        }
    }
    if (notes.empty())
    {
        return changeNotes(events, which, [](const Note& note, Tick) { return note.velocity; });
    }
    const double mean = sum / static_cast<double>(notes.size());
    const auto local = [&](Tick tick)
    {
        const auto half = dynamics.window ? std::max<Tick>(dynamics.window(tick), 1) / 2 : 0;
        double total = 0;
        int count = 0;
        for (const auto i : notes)
        {
            if (events[i].tick >= tick - half && events[i].tick <= tick + half)
            {
                total += std::get<Note>(events[i].data).velocity;
                ++count;
            }
        }
        return total / count;
    };
    const double amount = dynamics.percent / 100.0;
    return changeNotes(
        events, which,
        [&](const Note& note, Tick tick)
        {
            const double v = note.velocity;
            if ((dynamics.above && v <= *dynamics.above) ||
                (dynamics.below && v >= *dynamics.below))
            {
                return note.velocity;
            }
            const double around = local(tick);
            if (dynamics.kind == Kind::Shape)
            {
                return eased(note.velocity, v + (around - mean) * (amount - 1.0), limits);
            }
            const double centre = dynamics.centre == Centre::Local     ? around
                                  : dynamics.centre == Centre::Average ? mean
                                                                       : dynamics.fixed;
            const double by = dynamics.kind == Kind::Compress ? 1.0 - amount : 1.0 + amount;
            return eased(note.velocity, centre + (v - centre) * by, limits);
        });
}

std::vector<std::vector<std::size_t>>
chordsOf(const EventList& events, std::span<const std::size_t> which,
         const std::function<bool(Tick first, Tick tick)>& together)
{
    std::vector<std::size_t> notes;
    for (const auto i : which)
    {
        if (std::holds_alternative<Note>(events[i].data))
        {
            notes.push_back(i);
        }
    }
    std::ranges::stable_sort(notes, {}, [&](std::size_t i) { return events[i].tick; });
    std::vector<std::vector<std::size_t>> chords;
    std::optional<Tick> first;
    for (const auto i : notes)
    {
        if (!first || !together(*first, events[i].tick))
        {
            first = events[i].tick;
            chords.emplace_back();
        }
        chords.back().push_back(i);
    }
    for (auto& chord : chords)
    {
        std::ranges::stable_sort(chord, {}, [&](std::size_t i)
                                 { return std::get<Note>(events[i].data).pitch; });
    }
    return chords;
}

Transformed chordVelocity(const EventList& events, std::span<const std::size_t> which,
                          const std::function<bool(Tick first, Tick tick)>& together,
                          ChordVelocity how, VelocityRamp amount, VelocityLimits limits)
{
    // Each note's new velocity by its index, then the notes taken changed to them.
    std::vector<std::pair<std::size_t, std::uint8_t>> to;
    for (const auto& chord : chordsOf(events, which, together))
    {
        if (chord.size() < 2)
        {
            continue;
        }
        double sum = 0;
        for (const auto i : chord)
        {
            sum += std::get<Note>(events[i].data).velocity;
        }
        const double mean = sum / static_cast<double>(chord.size());
        for (std::size_t n = 0; n < chord.size(); ++n)
        {
            const auto v = std::get<Note>(events[chord[n]].data).velocity;
            const double along = static_cast<double>(n) / static_cast<double>(chord.size() - 1);
            const double by = amount.from / 100.0;
            const auto changed =
                how == ChordVelocity::Accent
                    ? eased(v, v + (n + 1 == chord.size() ? amount.from : -amount.from), limits)
                : how == ChordVelocity::Balance
                    ? limited(amount.from + (amount.to - amount.from) * along, limits)
                : how == ChordVelocity::Compress ? eased(v, mean + (v - mean) * (1.0 - by), limits)
                                                 : eased(v, mean + (v - mean) * (1.0 + by), limits);
            to.emplace_back(chord[n], changed);
        }
    }
    auto changed = takenEvents(events, which);
    for (std::size_t n = 0; n < which.size(); ++n)
    {
        const auto found =
            std::ranges::find(to, which[n], &std::pair<std::size_t, std::uint8_t>::first);
        if (found != to.end())
        {
            std::get<Note>(changed[n].data).velocity = found->second;
        }
    }
    return finish(events, which, changed, false);
}

std::optional<VelocityRamp> parseVelocityRange(std::string_view text, bool percentages)
{
    text = trimmed(text);
    const auto dash = text.find('-');
    if (dash == std::string_view::npos)
    {
        return std::nullopt;
    }
    auto from = trimmed(text.substr(0, dash));
    auto to = trimmed(text.substr(dash + 1));
    const bool percent = from.ends_with('%') && to.ends_with('%');
    if (percent != (from.ends_with('%') || to.ends_with('%')) || (percent && !percentages))
    {
        return std::nullopt;
    }
    if (percent)
    {
        from.remove_suffix(1);
        to.remove_suffix(1);
    }
    const auto a = number(from);
    const auto b = number(to);
    const int most = percent ? 1000 : 127;
    const int least = percent ? 0 : 1;
    if (!a || !b || *a < least || *a > most || *b < least || *b > most)
    {
        return std::nullopt;
    }
    return VelocityRamp{percent, *a, *b};
}

std::optional<int> parsePercent(std::string_view text)
{
    text = trimmed(text);
    if (text.ends_with('%'))
    {
        text.remove_suffix(1);
    }
    const auto value = number(text);
    if (!value || *value < 0 || *value > 100)
    {
        return std::nullopt;
    }
    return value;
}

Transformed humanize(const EventList& events, std::span<const std::size_t> which,
                     const std::function<Tick(Tick)>& maxShift, int maxVelocity, Tick end,
                     const std::function<std::int64_t(std::int64_t, std::int64_t)>& random)
{
    auto changed = takenEvents(events, which);
    for (auto& event : changed)
    {
        auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr)
        {
            continue;
        }
        const auto most = std::max<Tick>(maxShift(event.tick), 0);
        event.tick =
            std::clamp<Tick>(event.tick + random(-most, most), 0, std::max<Tick>(end - 1, 0));
        note->velocity =
            clampedVelocity(std::int64_t{note->velocity} + random(-maxVelocity, maxVelocity));
    }
    return finish(events, which, changed, true);
}

EventList cutAt(const EventList& events, Tick tick, const std::function<bool(const Note&)>& which)
{
    EventList cut;
    for (const auto& event : events.all())
    {
        const auto* note = std::get_if<Note>(&event.data);
        if (note == nullptr || event.tick >= tick || event.tick + note->duration <= tick ||
            !which(*note))
        {
            cut.insert(event);
            continue;
        }
        auto before = *note;
        before.duration = tick - event.tick;
        before.releaseVelocity = Note{}.releaseVelocity;
        auto after = *note;
        after.duration = event.tick + note->duration - tick;
        cut.insert({event.tick, before});
        cut.insert({tick, after});
    }
    return cut;
}

} // namespace modal::core
