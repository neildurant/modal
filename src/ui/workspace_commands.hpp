// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The commands the workspace runs itself, every view sharing them, and the list of all the
// commands it registers, each with where it does something. Private to the ui library
// (split from workspace.cpp, November 2026, after a code review).

#include <modal/core/meter_map.hpp>
#include <modal/ui/commands.hpp>

#include <array>
#include <span>
#include <string_view>

namespace modal::ui
{

std::span<const CommandInfo> sharedCommands();

// Motions that only move the cursor, rather than landing on a boundary they name, so aren't
// ranges after an operator (see Ranges, objects and putting in the design): notes and chords,
// which objects take. 0, $, { and } are ranges (decided November 2026, after a review of the
// grammar): 0 and $ to the start and the end of the bar in the piano roll, or the first and the
// last bar in the arrangement, landed on included, and { and } to the same place a bar before
// or after, not.
inline constexpr std::array<std::string_view, 4> kMovesOnly{
    "cursor-note-next", "cursor-note-previous", "cursor-chord-next", "cursor-chord-previous"};

// The meters :bar-insert offers before any others the song has or were typed.
std::span<const core::TimeSignature> offeredMeters();

} // namespace modal::ui
