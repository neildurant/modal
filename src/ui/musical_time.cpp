// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/musical_time.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace modal::ui
{
namespace
{
struct Parts
{
    std::int64_t bar;
    core::Tick step;
    core::Tick fine;
};

Parts startParts(core::Tick tick, const core::MeterMap& meter, core::Tick unit)
{
    auto bar = meter.toBarBeatTick(tick).bar;
    const auto start = meter.barStart(bar);
    const auto length = meter.barStart(bar + 1) - start;
    const auto relative = tick - start;
    auto step = relative / unit + (relative % unit > unit / 2 ? 1 : 0);
    step = std::min(step, length / unit);
    const auto fine = relative - step * unit;
    if (step * unit == length)
    {
        ++bar;
        step = 0;
    }
    return {bar, step + 1, fine};
}

Parts lengthParts(core::Tick ticks, core::Tick unit)
{
    const auto steps = ticks / unit + (ticks % unit > unit / 2 ? 1 : 0);
    return {0, steps, ticks - steps * unit};
}

std::string fineText(core::Tick ticks)
{
    return (ticks >= 0 ? "+" : "") + std::to_string(ticks) + "t";
}

std::optional<std::int64_t> readInteger(std::string_view text)
{
    if (text.ends_with('t'))
    {
        text.remove_suffix(1);
    }
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
        if (text.starts_with('-') || text.starts_with('+'))
        {
            return std::nullopt;
        }
    }
    if (text.empty())
    {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

core::Tick ticksIn(core::NoteFraction unit, core::Ppqn ppqn)
{
    return std::max<core::Tick>(1, unit.toTicks(ppqn));
}
} // namespace

bool isMusicalTimingField(std::string_view heading)
{
    return heading == "Start bar" || heading == "Start step" || heading == "Start fine" ||
           heading == "Length steps" || heading == "Length fine";
}

void addMusicalTiming(Inspection& inspection, const core::Event& event, const core::MeterMap& meter,
                      core::NoteFraction unit)
{
    const auto* note = std::get_if<core::Note>(&event.data);
    if (!note)
    {
        return;
    }
    const auto ticks = ticksIn(unit, meter.ppqn());
    const auto start = startParts(event.tick, meter, ticks);
    const auto length = lengthParts(note->duration, ticks);
    const auto add = [&](std::string heading, std::string value, bool editable = true)
    { inspection.fields.push_back({std::move(heading), std::move(value), editable}); };
    add("Units", core::toString(unit) + (unit.isExact(meter.ppqn()) ? "" : " (rounded)"), false);
    add("Start bar", std::to_string(start.bar));
    add("Start step", std::to_string(start.step));
    add("Start fine", fineText(start.fine));
    add("Length steps", std::to_string(length.step));
    add("Length fine", fineText(length.fine));

    // Tab follows the visual groups: note properties, coarse/fine pairs, then exact timing.
    constexpr std::array<std::string_view, 13> order{
        "Pitch",      "Velocity",     "Release",     "Units",     "Start step",
        "Start fine", "Length steps", "Length fine", "Start bar", "Position",
        "Length",     "Ends",         "Time"};
    std::ranges::stable_sort(inspection.fields, {}, [&](const InspectorField& field)
                             { return std::ranges::find(order, field.heading) - order.begin(); });
}

EditResult setMusicalTiming(const core::Event& event, std::string_view heading,
                            std::string_view text, const core::MeterMap& meter,
                            core::NoteFraction unit)
{
    const auto* note = std::get_if<core::Note>(&event.data);
    if (!note || !isMusicalTimingField(heading))
    {
        return std::string{"Not a note timing field"};
    }
    const auto number = readInteger(text);
    // A bounded entry keeps bar and step multiplication safe at every supported PPQN.
    if (!number || *number < -1'000'000 || *number > 1'000'000)
    {
        return std::string{"Enter a whole number between -1000000 and 1000000"};
    }
    const auto ticks = ticksIn(unit, meter.ppqn());
    auto result = event;
    if (heading.starts_with("Start"))
    {
        auto parts = startParts(event.tick, meter, ticks);
        if (heading == "Start bar")
        {
            parts.bar = *number;
        }
        else if (heading == "Start step")
        {
            parts.step = *number;
        }
        else
        {
            parts.fine = *number;
        }
        if (parts.bar < 1 || parts.step < 1)
        {
            return std::string{"Bars and start steps count from 1"};
        }
        const auto start = meter.barStart(parts.bar);
        const auto end = meter.barStart(parts.bar + 1);
        if (parts.step - 1 > (end - start - 1) / ticks)
        {
            return std::string{"That step is outside the bar"};
        }
        const auto coarse = start + (parts.step - 1) * ticks;
        if (parts.fine < -coarse)
        {
            return std::string{"A note cannot start before the song"};
        }
        result.tick = coarse + parts.fine;
    }
    else
    {
        auto parts = lengthParts(note->duration, ticks);
        if (heading == "Length steps")
        {
            parts.step = *number;
        }
        else
        {
            parts.fine = *number;
        }
        if (parts.step < 0 || parts.step > std::numeric_limits<core::Tick>::max() / ticks)
        {
            return std::string{"Length steps must be zero or more"};
        }
        const auto coarse = parts.step * ticks;
        if (parts.fine <= -coarse || parts.fine > std::numeric_limits<core::Tick>::max() - coarse)
        {
            return std::string{"A note must have a positive length"};
        }
        std::get<core::Note>(result.data).duration = coarse + parts.fine;
    }
    return result;
}

EditResult stepMusicalTiming(const core::Event& event, std::string_view heading, int by,
                             const core::MeterMap& meter, core::NoteFraction unit)
{
    const auto* note = std::get_if<core::Note>(&event.data);
    if (!note)
    {
        return std::string{"No note to edit"};
    }
    const auto ticks = ticksIn(unit, meter.ppqn());
    const auto start = startParts(event.tick, meter, ticks);
    const auto length = lengthParts(note->duration, ticks);
    const auto value = heading == "Start bar"      ? start.bar
                       : heading == "Start step"   ? start.step
                       : heading == "Start fine"   ? start.fine
                       : heading == "Length steps" ? length.step
                                                   : length.fine;
    return setMusicalTiming(event, heading, std::to_string(value + by), meter, unit);
}
} // namespace modal::ui
