// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/chords.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace modal::core
{

namespace
{

constexpr std::array<std::string_view, 12> kNames{"C",  "Db", "D",  "Eb", "E",  "F",
                                                  "F#", "G",  "Ab", "A",  "Bb", "B"};

// The intervals above a chord's root, its 0 among them, as bits.
constexpr unsigned intervals(std::initializer_list<int> semitones)
{
    unsigned bits = 0;
    for (const int semitone : semitones)
    {
        bits |= 1U << static_cast<unsigned>(semitone);
    }
    return bits;
}

struct Quality
{
    std::string_view suffix;
    unsigned intervals;
};

// Those with the same notes as another are later, so the first wins: C6 before Am7/C is
// decided by the root on the bass, not by this order.
constexpr std::array<Quality, 23> kQualities{{
    {"", intervals({0, 4, 7})},
    {"m", intervals({0, 3, 7})},
    {"dim", intervals({0, 3, 6})},
    {"aug", intervals({0, 4, 8})},
    {"sus2", intervals({0, 2, 7})},
    {"sus4", intervals({0, 5, 7})},
    {"6", intervals({0, 4, 7, 9})},
    {"m6", intervals({0, 3, 7, 9})},
    {"7", intervals({0, 4, 7, 10})},
    {"maj7", intervals({0, 4, 7, 11})},
    {"m7", intervals({0, 3, 7, 10})},
    {"mmaj7", intervals({0, 3, 7, 11})},
    {"m7b5", intervals({0, 3, 6, 10})},
    {"dim7", intervals({0, 3, 6, 9})},
    {"7sus4", intervals({0, 5, 7, 10})},
    {"add9", intervals({0, 2, 4, 7})},
    {"madd9", intervals({0, 2, 3, 7})},
    {"9", intervals({0, 2, 4, 7, 10})},
    {"maj9", intervals({0, 2, 4, 7, 11})},
    {"m9", intervals({0, 2, 3, 7, 10})},
    // Sevenths with no fifth, as they're often voiced.
    {"7", intervals({0, 4, 10})},
    {"maj7", intervals({0, 4, 11})},
    {"m7", intervals({0, 3, 10})},
}};

using Classes = std::bitset<12>;

std::optional<std::string> named(Classes classes, int bass)
{
    if (classes.count() < 3)
    {
        return std::nullopt;
    }
    // Roots from the bass up, so the bass is tried first.
    for (int step = 0; step < 12; ++step)
    {
        const int root = (bass + step) % 12;
        if (!classes[static_cast<std::size_t>(root)])
        {
            continue;
        }
        Classes above;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (classes[static_cast<std::size_t>(pc)])
            {
                above.set(static_cast<std::size_t>((pc - root + 12) % 12));
            }
        }
        for (const auto& quality : kQualities)
        {
            if (Classes{quality.intervals} == above)
            {
                std::string name{kNames[static_cast<std::size_t>(root)]};
                name += quality.suffix;
                if (root != bass)
                {
                    name += "/";
                    name += kNames[static_cast<std::size_t>(bass)];
                }
                return name;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> chordName(std::span<const std::uint8_t> pitches)
{
    if (pitches.empty())
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> sorted(pitches.begin(), pitches.end());
    std::ranges::sort(sorted);
    const auto classesOf = [](std::span<const std::uint8_t> notes)
    {
        Classes classes;
        for (const auto pitch : notes)
        {
            classes.set(pitch % 12);
        }
        return classes;
    };
    const int bass = sorted.front() % 12;
    if (auto name = named(classesOf(sorted), bass))
    {
        return name;
    }
    // A melody note over a chord: the chord without it, if that changes what's there.
    const std::span<const std::uint8_t> below{sorted.data(), sorted.size() - 1};
    if (below.size() >= 3 && classesOf(below) != classesOf(sorted))
    {
        return named(classesOf(below), bass);
    }
    return std::nullopt;
}

} // namespace modal::core
