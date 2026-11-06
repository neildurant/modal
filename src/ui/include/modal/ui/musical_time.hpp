// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/event_edit.hpp>
#include <modal/ui/inspector.hpp>

namespace modal::ui
{

// Coarse steps are counted from each bar line; fine ticks are the signed distance to the
// nearest step. Length steps count from zero, start steps and bars from one.
void addMusicalTiming(Inspection& inspection, const core::Event& event, const core::MeterMap& meter,
                      core::NoteFraction unit);
bool isMusicalTimingField(std::string_view heading);
EditResult setMusicalTiming(const core::Event& event, std::string_view heading,
                            std::string_view text, const core::MeterMap& meter,
                            core::NoteFraction unit);
EditResult stepMusicalTiming(const core::Event& event, std::string_view heading, int by,
                             const core::MeterMap& meter, core::NoteFraction unit);

} // namespace modal::ui
