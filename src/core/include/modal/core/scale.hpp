// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::core
{

// A scale, by the semitones of its notes above the tonic, from 0, rising, under 12: major is
// 0 2 4 5 7 9 11. Any number of notes, five for a pentatonic one.
struct Scale
{
    std::string name;
    std::vector<int> steps;
    std::string aliases = {}; // other names, separated by spaces: ionian for major

    friend bool operator==(const Scale&, const Scale&) = default;
};

// The scales built in (decided November 2026): major and minor, harmonic and melodic minor, the
// modes, the pentatonics, blues, whole tone and the two diminished scales.
std::span<const Scale> builtInScales();

// A scale's degrees as written in a modalrc's scale command: 1 b2 3 4 5 b6 b7, each 1 to 7
// with any flats or sharps before it, starting at 1 and rising; or why not.
std::variant<std::vector<int>, std::string> parseDegrees(std::string_view text);

// A key: a tonic, spelled as given (Eb, not D#), and a scale.
struct Key
{
    int tonic = 0; // 0 for C to 11 for B
    std::string tonicName = "C";
    Scale scale;

    // As it's typed: "D dorian".
    std::string text() const { return tonicName + " " + scale.name; }

    // Where a note is: the scale's note at or below it, as a degree counted from the tonic's
    // in octave -1 (so every note's is 0 or more), and how many semitones above that it is,
    // 0 for a note in the scale.
    struct Place
    {
        int degree;
        int above;
    };
    Place place(int pitch) const;
    int pitchOf(int degree) const;
    bool holds(int pitch) const { return place(pitch).above == 0; }

    friend bool operator==(const Key&, const Key&) = default;
};

// A key as typed: a tonic (D, Eb, F#) and a scale's name or alias (dorian), in either case,
// major if there's none; or a tonic and m for minor (Dm). None if it isn't one.
std::optional<Key> parseKey(std::string_view text, std::span<const Scale> scales);

// The scale of that name or alias, ignoring case, among those.
const Scale* findScale(std::string_view name, std::span<const Scale> scales);

// The tonics as offered for typing: C, C#, Db, D and so on.
std::span<const std::string_view> tonicNames();

} // namespace modal::core
