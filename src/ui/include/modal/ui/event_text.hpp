// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/core/tick.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace modal::ui
{

// How many digits the ticks of a position take: enough for a quarter note's, so the columns
// line up at any resolution, 3 at 960 PPQN.
int tickDigits(core::Ppqn ppqn);

// A position as bar:beat:tick in a meter, the ticks padded with zeros: 17:2:040.
std::string positionText(const core::MeterMap& meter, core::Tick tick);

// A note's length as bars, beats and ticks in the meter where it starts, and in brackets the
// common note value it is or is close to (within 5%), if there's one: straight from a whole
// note to a 1/16, dotted and triplet to 1/8. and 1/8t, and whole notes beyond. 0:0:480 (1/8),
// 0:0:492 (≈1/8), or 0:1:252 alone.
std::string lengthText(core::Tick length, core::Ppqn ppqn, core::TimeSignature signature);

// A channel message come in, for the In light, always 13 characters so the status line keeps
// still: its channel, from 1, then what it is and its value, " 1 C#4    127", "10 CC64   127",
// " 1 Prog     5", " 1 Bend +8191", " 1 Pres    64", " 1 PPrs    64"; the bend from -8192 to
// +8191, 0 at rest. Nothing but spaces for nothing yet.
std::string inputText(std::span<const std::uint8_t> message);

} // namespace modal::ui
