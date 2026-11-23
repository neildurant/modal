// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/tick.hpp>
#include <modal/ui/view.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace modal::ui
{

// A register as the system clipboard holds it (decided 23 November 2026): a header line of its
// own, saying what kind of copy it is and what the song format can't (the copy's span, whether
// it was taken as bars, the pitch Alt-p puts at), then a small song in the native format
// holding it. Events and ramps are a section of their tracks; sections are those sections with
// the tracks they were taken from; whole tracks are those tracks with all their parts.
//
//   modal-clip 1 events ppqn=960 span=3840 bars meters=1
//   modal-song 1
//   ...
//
// Nothing for an empty register, or one whose parts don't fit together as a song.
std::optional<std::string> clipboardText(const EventRegister& reg, core::Ppqn ppqn);

// Whether text is a copy from Modal, by its first line, before reading it.
bool isClipboardCopy(std::string_view text);

// A copy read back at a song's resolution, rescaled if it was made at another; or why not.
struct ClipboardRead
{
    std::optional<EventRegister> reg;
    std::string error;
};
ClipboardRead readClipboard(std::string_view text, core::Ppqn ppqn);

} // namespace modal::ui
