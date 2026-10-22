// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/song.hpp>
#include <modal/core/tick.hpp>

namespace modal::ui
{

// A made-up song for the test view's arrangement page: sections in several meters, changing
// on every track at once, with a single extra bar of 2/4 ending the first chorus; two tracks
// in meters of their own, whose bars start again at each section; notes at four densities,
// with some bars left empty; and a few empty tracks.
core::Song demoSong(core::Ppqn ppqn = core::kPpqn);

} // namespace modal::ui
