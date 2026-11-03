// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The commands the workspace runs itself, every view sharing them, and the list of all the
// commands it registers, each with where it does something. Private to the ui library
// (split from workspace.cpp, November 2026, after a code review).

#include <modal/core/meter_map.hpp>
#include <modal/ui/commands.hpp>

#include <span>

namespace modal::ui
{

std::span<const CommandInfo> sharedCommands();

// The meters :bar-insert offers before any others the song has or were typed.
std::span<const core::TimeSignature> offeredMeters();

} // namespace modal::ui
