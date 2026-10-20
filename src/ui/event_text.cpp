#include <modal/core/note_fraction.hpp>
#include <modal/ui/event_text.hpp>

#include <cstdlib>
#include <vector>

namespace modal::ui
{

namespace
{

// A note length shows as a note value as it is when its denominator divides this: down to
// 1/64 notes, dotted (3/128) and as triplets (1/96).
constexpr std::int64_t kExactDenominators = 384;

std::string padded(core::Tick tick, int digits)
{
    auto text = std::to_string(tick);
    return text.size() < static_cast<std::size_t>(digits)
               ? std::string(static_cast<std::size_t>(digits) - text.size(), '0') + text
               : text;
}

// The common lengths a length is shown against, with its difference: the straight values
// from a whole note to 1/64, their dotted and triplet values, and whole notes beyond.
std::vector<core::NoteFraction> commonLengths(core::Tick length, core::Ppqn ppqn)
{
    std::vector<core::NoteFraction> lengths;
    for (std::int64_t d = 1; d <= 64; d *= 2)
    {
        const core::NoteFraction straight{1, d};
        lengths.push_back(straight);
        lengths.push_back(straight.dotted());
        lengths.push_back(straight.triplet());
    }
    for (std::int64_t wholes = 2; wholes <= length / ppqn.whole() + 1; ++wholes)
    {
        lengths.push_back({wholes, 1});
    }
    return lengths;
}

} // namespace

int tickDigits(core::Ppqn ppqn)
{
    return static_cast<int>(std::to_string(ppqn.quarter() - 1).size());
}

std::string positionText(const core::MeterMap& meter, core::Tick tick)
{
    const auto position = meter.toBarBeatTick(tick);
    return std::to_string(position.bar) + ":" + std::to_string(position.beat) + ":" +
           padded(position.tick, tickDigits(meter.ppqn()));
}

std::string lengthText(core::Tick length, core::Ppqn ppqn, LengthFormat format,
                       core::TimeSignature signature)
{
    if (format == LengthFormat::Beats)
    {
        const core::Tick bar = signature.barLength().toTicks(ppqn);
        const core::Tick beat = signature.beatLength().toTicks(ppqn);
        return std::to_string(length / bar) + ":" + std::to_string(length % bar / beat) + ":" +
               padded(length % beat, tickDigits(ppqn));
    }
    if (length <= 0)
    {
        return "0";
    }
    const core::NoteFraction exact{length, ppqn.whole()};
    if (kExactDenominators % exact.denominator() == 0)
    {
        return toString(exact);
    }

    // The nearest common length, the shorter if two are as near, and how far off it is.
    const auto lengths = commonLengths(length, ppqn);
    if (length < core::NoteFraction{1, 64}.triplet().toTicks(ppqn))
    {
        return std::to_string(length) + (length == 1 ? " tick" : " ticks");
    }
    auto nearest = lengths.front();
    for (const auto& candidate : lengths)
    {
        const auto distance = std::abs(length - candidate.toTicks(ppqn));
        const auto best = std::abs(length - nearest.toTicks(ppqn));
        if (distance < best || (distance == best && candidate < nearest))
        {
            nearest = candidate;
        }
    }
    const core::Tick off = length - nearest.toTicks(ppqn);
    return toString(nearest) + (off > 0 ? " + " : " - ") + std::to_string(std::abs(off));
}

} // namespace modal::ui
