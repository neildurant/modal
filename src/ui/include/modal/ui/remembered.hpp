// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/meter_map.hpp>
#include <modal/ui/key.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// What Modal remembers between sessions without being told (decided October 2026): how often
// and how lately each command was run from the palette, which ranks it there, and the meters
// typed that weren't offered, offered again after, and the macros recorded, being the user's
// tools rather than a song's. Kept as plain text in the state directory, written by Modal and
// safe to delete.
class Remembered
{
public:
    static constexpr std::size_t kMaxMeters = 12;

    // A command run from the palette, at a time in seconds since 1970.
    void ran(std::string_view command, std::int64_t now);

    // How highly a command ranks in the palette: how often it's been run, the recent counting
    // for more (within the hour eight times as much as over a month ago); 0 if never.
    double rank(std::string_view command, std::int64_t now) const;

    // Forgets the commands run (:palette-forget), keeping the meters.
    void forgetCommands();

    // A meter typed that wasn't offered, and those remembered, the latest first.
    void typedMeter(core::TimeSignature meter);
    std::span<const core::TimeSignature> meters() const { return meters_; }

    // The piano roll's lanes used, as their names (cc74), the latest first.
    static constexpr std::size_t kMaxLanes = 16;
    void usedLane(std::string lane);
    std::span<const std::string> lanes() const { return lanes_; }

    // Song files opened or saved, the latest first, each with when it was last, in seconds
    // since 1970 (decided 20 November 2026), and the song's title then (decided 23 November
    // 2026): kept to a number given, a file used again moving to the top.
    struct RecentSong
    {
        std::string path; // as UTF-8
        std::int64_t used = 0;
        std::string title; // empty for a song without one

        friend bool operator==(const RecentSong&, const RecentSong&) = default;
    };
    static constexpr std::size_t kMaxSongs = 100; // the most a setting can ask for
    void usedSong(std::string path, std::int64_t now, std::size_t most, std::string title = {});
    bool forgetSong(std::string_view path);
    std::span<const RecentSong> songs() const { return songs_; }

    // The macros, a to z, as their keys; setting one to no keys forgets it.
    void setMacro(char32_t name, std::vector<Key> keys);
    const std::vector<Key>* macro(char32_t name) const;
    const std::map<char32_t, std::vector<Key>>& macros() const { return macros_; }

    // As the file has it, and from it, passing over lines it doesn't know.
    std::string text() const;
    static Remembered read(std::string_view text);

    friend bool operator==(const Remembered&, const Remembered&) = default;

private:
    struct Use
    {
        std::string command;
        std::int64_t count = 0;
        std::int64_t last = 0;

        friend bool operator==(const Use&, const Use&) = default;
    };

    std::vector<Use> uses_;
    std::vector<core::TimeSignature> meters_;
    std::vector<std::string> lanes_;
    std::map<char32_t, std::vector<Key>> macros_;
    std::vector<RecentSong> songs_;
};

} // namespace modal::ui
