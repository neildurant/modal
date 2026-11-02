// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/pitch.hpp>
#include <modal/core/scale.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace modal::core
{

namespace
{

std::string lowered(std::string_view text)
{
    std::string lower{text};
    std::ranges::transform(lower, lower.begin(),
                           [](char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

int floorDivide(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

std::vector<std::string_view> words(std::string_view text)
{
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < text.size())
    {
        const auto start = text.find_first_not_of(" \t", at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto end = std::min(text.find_first_of(" \t", start), text.size());
        found.push_back(text.substr(start, end - start));
        at = end;
    }
    return found;
}

} // namespace

std::span<const Scale> builtInScales()
{
    static const std::array<Scale, 15> kScales{{
        {"major", {0, 2, 4, 5, 7, 9, 11}, "ionian"},
        {"minor", {0, 2, 3, 5, 7, 8, 10}, "aeolian"},
        {"harmonic-minor", {0, 2, 3, 5, 7, 8, 11}},
        {"melodic-minor", {0, 2, 3, 5, 7, 9, 11}},
        {"dorian", {0, 2, 3, 5, 7, 9, 10}},
        {"phrygian", {0, 1, 3, 5, 7, 8, 10}},
        {"lydian", {0, 2, 4, 6, 7, 9, 11}},
        {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
        {"locrian", {0, 1, 3, 5, 6, 8, 10}},
        {"major-pentatonic", {0, 2, 4, 7, 9}, "pentatonic"},
        {"minor-pentatonic", {0, 3, 5, 7, 10}},
        {"blues", {0, 3, 5, 6, 7, 10}},
        {"whole-tone", {0, 2, 4, 6, 8, 10}},
        {"diminished", {0, 2, 3, 5, 6, 8, 9, 11}, "whole-half"},
        {"dominant-diminished", {0, 1, 3, 4, 6, 7, 9, 10}, "half-whole"},
    }};
    return kScales;
}

std::variant<std::vector<int>, std::string> parseDegrees(std::string_view text)
{
    constexpr std::array<int, 7> kMajor{0, 2, 4, 5, 7, 9, 11};
    std::vector<int> steps;
    for (auto word : words(text))
    {
        int shift = 0;
        while (!word.empty() && (word.front() == 'b' || word.front() == '#'))
        {
            shift += word.front() == 'b' ? -1 : 1;
            word.remove_prefix(1);
        }
        if (word.size() != 1 || word[0] < '1' || word[0] > '7')
        {
            return "a scale's degrees are 1 to 7, with b or # before them, as 1 b2 3 4 5 b6 b7";
        }
        const int step = kMajor[static_cast<std::size_t>(word[0] - '1')] + shift;
        if ((steps.empty() && step != 0) || (!steps.empty() && step <= steps.back()) || step >= 12)
        {
            return "a scale's degrees start at 1 and rise within the octave";
        }
        steps.push_back(step);
    }
    if (steps.size() < 2)
    {
        return "a scale needs two degrees or more, as 1 b2 3 4 5 b6 b7";
    }
    return steps;
}

Key::Place Key::place(int pitch) const
{
    // Counted from the tonic in octave -1, so as not to go below 0.
    const int size = static_cast<int>(scale.steps.size());
    const int from = pitch - (tonic - 12);
    const int octave = floorDivide(from, 12);
    const int within = from - octave * 12;
    const auto above = std::ranges::upper_bound(scale.steps, within);
    const int index = static_cast<int>(above - scale.steps.begin()) - 1;
    return {octave * size + index, within - scale.steps[static_cast<std::size_t>(index)]};
}

int Key::pitchOf(int degree) const
{
    const int size = static_cast<int>(scale.steps.size());
    const int octave = floorDivide(degree, size);
    const int index = degree - octave * size;
    return tonic - 12 + octave * 12 + scale.steps[static_cast<std::size_t>(index)];
}

const Scale* findScale(std::string_view name, std::span<const Scale> scales)
{
    const auto wanted = lowered(name);
    // The last of a name, so a modalrc's scale replaces a built-in one.
    const Scale* found = nullptr;
    for (const auto& scale : scales)
    {
        const auto aliases = words(scale.aliases);
        if (scale.name == wanted || std::ranges::find(aliases, wanted) != aliases.end())
        {
            found = &scale;
        }
    }
    return found;
}

std::optional<Key> parseKey(std::string_view text, std::span<const Scale> scales)
{
    const auto parts = words(text);
    if (parts.empty() || parts.size() > 2)
    {
        return std::nullopt;
    }
    auto tonicName = parts[0];
    auto scaleName = parts.size() == 2 ? parts[1] : std::string_view{"major"};
    if (parts.size() == 1 && tonicName.size() > 1 && tonicName.ends_with('m') &&
        parsePitchClass(tonicName.substr(0, tonicName.size() - 1)))
    {
        tonicName.remove_suffix(1);
        scaleName = "minor";
    }
    const auto tonic = parsePitchClass(tonicName);
    const auto* scale = findScale(scaleName, scales);
    if (!tonic || scale == nullptr)
    {
        return std::nullopt;
    }
    std::string spelled{tonicName};
    spelled[0] = static_cast<char>(std::toupper(spelled[0]));
    return Key{*tonic, spelled, *scale};
}

std::span<const std::string_view> tonicNames()
{
    static constexpr std::array<std::string_view, 17> kNames{"C",  "C#", "Db", "D",  "D#", "Eb",
                                                             "E",  "F",  "F#", "Gb", "G",  "G#",
                                                             "Ab", "A",  "A#", "Bb", "B"};
    return kNames;
}

} // namespace modal::core
