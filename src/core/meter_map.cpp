#include <modal/core/meter_map.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <iterator>
#include <utility>

namespace modal::core
{

std::optional<BarBeatTick> parseBarBeat(std::string_view text)
{
    const auto number = [](std::string_view part) -> std::optional<std::int64_t>
    {
        std::int64_t value = 0;
        const auto* end = part.data() + part.size();
        const auto [ptr, error] = std::from_chars(part.data(), end, value);
        if (part.empty() || error != std::errc{} || ptr != end || value < 1)
        {
            return std::nullopt;
        }
        return value;
    };

    const auto colon = text.find(':');
    const auto bar = number(text.substr(0, colon));
    const auto beat = colon == std::string_view::npos ? 1 : number(text.substr(colon + 1));
    if (!bar || !beat)
    {
        return std::nullopt;
    }
    return BarBeatTick{*bar, *beat, 0};
}

MeterMap::MeterMap(Ppqn ppqn, TimeSignature initial)
    : ppqn_(ppqn)
{
    assert(ppqn.value > 0);
    rebuild({{0, initial}});
}

Tick MeterMap::set(Tick tick, TimeSignature signature)
{
    assert(tick >= 0);
    std::vector<MeterChange> wanted;
    wanted.reserve(changes_.size() + 1);
    for (const auto& change : changes_)
    {
        if (change.tick < tick)
        {
            wanted.push_back(change);
        }
    }
    const std::size_t index = wanted.size();
    wanted.push_back({tick, signature});
    for (const auto& change : changes_)
    {
        if (change.tick > tick)
        {
            wanted.push_back(change);
        }
    }
    return rebuild(wanted)[index];
}

bool MeterMap::remove(Tick tick)
{
    if (tick == 0)
    {
        return false;
    }
    const auto it = std::ranges::find(changes_, tick, &MeterChange::tick);
    if (it == changes_.end())
    {
        return false;
    }
    std::vector<MeterChange> wanted = changes_;
    wanted.erase(wanted.begin() + std::distance(changes_.begin(), it));
    rebuild(wanted);
    return true;
}

TimeSignature MeterMap::signatureAt(Tick tick) const
{
    return changes_[changeAt(tick)].signature;
}

BarBeatTick MeterMap::toBarBeatTick(Tick tick) const
{
    assert(tick >= 0);
    const auto i = changeAt(tick);
    const auto& segment = segments_[i];
    const Tick offset = tick - changes_[i].tick;
    const Tick withinBar = offset % segment.barTicks;
    return {
        .bar = segment.firstBar + offset / segment.barTicks + 1,
        .beat = withinBar / segment.beatTicks + 1,
        .tick = withinBar % segment.beatTicks,
    };
}

std::optional<Tick> MeterMap::toTick(BarBeatTick position) const
{
    if (position.bar < 1 || position.beat < 1 || position.tick < 0)
    {
        return std::nullopt;
    }
    const Tick start = barStart(position.bar);
    const auto signature = signatureAt(start);
    const Tick beat = beatTicks(signature);
    if (position.beat > signature.numerator || position.tick >= beat)
    {
        return std::nullopt;
    }
    return start + (position.beat - 1) * beat + position.tick;
}

Tick MeterMap::barStart(std::int64_t bar) const
{
    assert(bar >= 1);
    const std::int64_t index = bar - 1;
    const auto it = std::ranges::upper_bound(segments_, index, {}, &Segment::firstBar);
    const auto i = static_cast<std::size_t>(std::distance(segments_.begin(), it)) - 1;
    return changes_[i].tick + (index - segments_[i].firstBar) * segments_[i].barTicks;
}

Tick MeterMap::beatAtOrAfter(Tick tick) const
{
    tick = std::max(tick, Tick{0});
    const auto i = changeAt(tick);
    const Tick beat = segments_[i].beatTicks;
    const Tick withinBeat = (tick - changes_[i].tick) % segments_[i].barTicks % beat;
    // A change always starts on a bar line, so the beat found never runs past one.
    return withinBeat == 0 ? tick : tick + beat - withinBeat;
}

Tick MeterMap::beatTicks(TimeSignature signature) const
{
    assert(signature.isValid());
    assert(signature.beatLength().isExact(ppqn_));
    return signature.beatLength().toTicks(ppqn_);
}

std::size_t MeterMap::changeAt(Tick tick) const
{
    // The last change at or before the tick; changes_[0] is at tick 0.
    const auto it = std::ranges::upper_bound(changes_, tick, {}, &MeterChange::tick);
    return static_cast<std::size_t>(std::distance(changes_.begin(), it)) - 1;
}

std::vector<Tick> MeterMap::rebuild(const std::vector<MeterChange>& wanted)
{
    assert(!wanted.empty() && wanted.front().tick == 0);

    // The signature for a remainder bar of the given length after a bar of `previous`:
    // whole beats of the previous meter if possible, otherwise the coarsest finer
    // power-of-two denominator that fits exactly.
    const auto remainderSignature = [this](Tick length,
                                           TimeSignature previous) -> std::optional<TimeSignature>
    {
        for (auto denominator = previous.denominator; denominator <= TimeSignature::kMaxDenominator;
             denominator *= 2)
        {
            const TimeSignature unit{1, denominator};
            if (!unit.beatLength().isExact(ppqn_))
            {
                break;
            }
            const Tick beat = unit.beatLength().toTicks(ppqn_);
            if (length % beat == 0 && length / beat <= TimeSignature::kMaxNumerator)
            {
                return TimeSignature{static_cast<std::int32_t>(length / beat), denominator};
            }
        }
        return std::nullopt;
    };

    std::vector<MeterChange> built{wanted.front()};
    std::vector<Tick> placed{0};
    for (std::size_t i = 1; i < wanted.size(); ++i)
    {
        auto [tick, signature] = wanted[i];
        const auto previous = built.back();

        // An earlier change moved on to a later beat and overtook this one: the later
        // change keeps the earlier one's position.
        if (tick <= previous.tick)
        {
            built.back().signature = signature;
            placed.push_back(previous.tick);
            continue;
        }

        const Tick bar = beatTicks(previous.signature) * previous.signature.numerator;
        Tick leftover = (tick - previous.tick) % bar;
        if (leftover != 0)
        {
            const Tick barStartTick = tick - leftover;
            auto remainder = remainderSignature(leftover, previous.signature);
            if (!remainder)
            {
                const Tick beat = beatTicks(previous.signature);
                tick += beat - leftover % beat;
                leftover = (tick - barStartTick) % bar;
                remainder = TimeSignature{static_cast<std::int32_t>(leftover / beat),
                                          previous.signature.denominator};
            }
            if (leftover != 0)
            {
                if (barStartTick == previous.tick)
                {
                    built.back().signature = *remainder;
                }
                else
                {
                    built.push_back({barStartTick, *remainder});
                }
            }
        }
        built.push_back({tick, signature});
        placed.push_back(tick);
    }

    // Repeating a signature changes nothing about the bars, so keep only the first.
    const auto [first, last] = std::ranges::unique(built, {}, &MeterChange::signature);
    built.erase(first, last);

    changes_ = std::move(built);
    updateSegments();
    return placed;
}

void MeterMap::updateSegments()
{
    segments_.clear();
    segments_.reserve(changes_.size());
    for (std::size_t i = 0; i < changes_.size(); ++i)
    {
        const Tick beat = beatTicks(changes_[i].signature);
        std::int64_t firstBar = 0;
        if (i > 0)
        {
            const auto& previous = segments_.back();
            firstBar =
                previous.firstBar + (changes_[i].tick - changes_[i - 1].tick) / previous.barTicks;
        }
        segments_.push_back({beat * changes_[i].signature.numerator, beat, firstBar});
    }
}

} // namespace modal::core
