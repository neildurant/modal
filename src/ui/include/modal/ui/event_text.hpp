#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/tick.hpp>

#include <cstdint>
#include <string>

namespace modal::ui
{

// How note lengths show (the lenformat setting).
enum class LengthFormat : std::uint8_t
{
    Note,  // a note value, with ticks over or under if it's not exact: 1/8, 1/8 + 12
    Beats, // bars, beats and ticks, in the meter where the note starts: 0:1:252
};

// How many digits the ticks of a position take: enough for a quarter note's, so the columns
// line up at any resolution, 3 at 960 PPQN.
int tickDigits(core::Ppqn ppqn);

// A position as bar:beat:tick in a meter, the ticks padded with zeros: 17:2:040.
std::string positionText(const core::MeterMap& meter, core::Tick tick);

// A note's length as the format has it, the beats in the meter where it starts.
std::string lengthText(core::Tick length, core::Ppqn ppqn, LengthFormat format,
                       core::TimeSignature signature);

} // namespace modal::ui
