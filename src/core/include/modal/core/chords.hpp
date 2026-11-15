// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace modal::core
{

// The name of the chord some notes make, as a lead sheet writes it (decided 15 November 2026):
// Fmaj7, Am7, Bm7b5, C/E. Its root and quality come from the notes' pitch classes, matched
// against the common chords (major, minor, diminished, augmented, sus2, sus4, 6ths, 7ths,
// maj7, m7b5, dim7, 9ths and add9, 7ths with no fifth too), a root on the lowest note
// preferred, and a slash before the lowest note where it isn't the root. Where the notes
// don't make one, the highest is left out, as a melody over a chord, and tried again. None
// for fewer than three pitch classes, or none that match. Roots and bass notes are spelled as
// lead sheets usually are, Db, Eb, F#, Ab and Bb, until songs have keys to spell them by.
std::optional<std::string> chordName(std::span<const std::uint8_t> pitches);

} // namespace modal::core
