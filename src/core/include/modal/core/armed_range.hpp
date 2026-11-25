// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/tick.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace modal::core
{

class Song;

// A span of a song armed for punch recording (decided 24 November 2026), and later for looping.
// It's kept as what it was armed from, so it follows that through edits: a section follows the
// section, moved, resized or renamed; two labels follow the labels, each by its section and
// name; a span of time chosen in Visual mode stays where it is in time.
struct ArmedRange
{
    struct OfSection
    {
        std::uint64_t section; // Section::id()
        friend bool operator==(const OfSection&, const OfSection&) = default;
    };
    struct LabelAt
    {
        std::uint64_t section; // the Section::id() of the section it's in
        std::string name;
        friend bool operator==(const LabelAt&, const LabelAt&) = default;
    };
    struct OfLabels
    {
        LabelAt from;
        LabelAt to;
        friend bool operator==(const OfLabels&, const OfLabels&) = default;
    };
    struct OfTime
    {
        Tick from;
        Tick to;
        friend bool operator==(const OfTime&, const OfTime&) = default;
    };

    std::variant<OfSection, OfLabels, OfTime> source;

    // A section by its index in the song.
    static ArmedRange section(const Song& song, std::size_t index);
    // Two of the song's labels, by their indices in Song::labels().
    static ArmedRange labels(const Song& song, std::size_t from, std::size_t to);
    static ArmedRange time(Tick from, Tick to) { return {OfTime{from, to}}; }

    friend bool operator==(const ArmedRange&, const ArmedRange&) = default;
};

// Where an armed range is in a song as it is now: its span, the end exclusive, or else why it
// no longer has one, to say as it's cleared.
struct ArmedSpan
{
    Tick from = 0;
    Tick to = 0;
    friend bool operator==(const ArmedSpan&, const ArmedSpan&) = default;
};
struct ArmedRangeFound
{
    std::optional<ArmedSpan> span;
    std::string gone; // why not, when there's no span

    friend bool operator==(const ArmedRangeFound&, const ArmedRangeFound&) = default;
};

// The punch and loop ranges where they are now, and whether looping is on, for showing them.
struct ArmedSpans
{
    std::optional<ArmedSpan> punch;
    std::optional<ArmedSpan> loop;
    bool loopOn = true;
};

// A span of time is cut short at the song's end.
ArmedRangeFound find(const Song& song, const ArmedRange& range);

// An armed range as a song file keeps it (decided 25 November 2026): sections by their index in
// the song, as their ids are the session's alone.
struct SavedRange
{
    struct OfSection
    {
        std::size_t section;
        friend bool operator==(const OfSection&, const OfSection&) = default;
    };
    struct LabelAt
    {
        std::size_t section;
        std::string name;
        friend bool operator==(const LabelAt&, const LabelAt&) = default;
    };
    struct OfLabels
    {
        LabelAt from;
        LabelAt to;
        friend bool operator==(const OfLabels&, const OfLabels&) = default;
    };

    std::variant<OfSection, OfLabels, ArmedRange::OfTime> source;

    friend bool operator==(const SavedRange&, const SavedRange&) = default;
};

// The range as the song would save it, or nothing where its section is gone.
std::optional<SavedRange> saved(const Song& song, const ArmedRange& range);
// The range again in a song, or nothing where it names a section the song hasn't.
std::optional<ArmedRange> restored(const Song& song, const SavedRange& range);

} // namespace modal::core
