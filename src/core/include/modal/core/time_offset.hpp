// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/note_fraction.hpp>
#include <modal/core/tempo_map.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace modal::core
{

// A playback displacement, retaining whether it follows musical or elapsed time, and for a
// musical one, how it was given: a note fraction and fine ticks (+1/16-33t) keep those parts,
// so they can be shown and stepped as chosen, and the fraction follows a change of PPQN.
class TimeOffset
{
public:
    static constexpr std::chrono::milliseconds kMaxMilliseconds{1000};
    static constexpr NoteFraction kMaxMusical{64, 1};

    TimeOffset() = default;
    TimeOffset(std::chrono::milliseconds value)
        : value_(value)
    {
    }
    static TimeOffset ticks(Tick value);
    // A note fraction, later or earlier, and a signed number of fine ticks on top.
    static TimeOffset fraction(NoteFraction coarse, bool earlier, Tick fine, Ppqn ppqn);

    bool musical() const { return std::holds_alternative<Tick>(value_); }
    // Ticks in all, or milliseconds.
    std::int64_t amount() const;
    bool valid(Ppqn ppqn) const;

    // The same offset with its fine part moved by an amount: ticks, or milliseconds.
    TimeOffset nudged(std::int64_t by) const;
    // A musical offset with whole steps of a note value added to its fraction, the fine part
    // kept; an offset of plain ticks gains their ticks, and stays in ticks.
    TimeOffset stepped(NoteFraction step, int steps, Ppqn ppqn) const;
    // The same offset at another resolution: the fraction stays, and the ticks scale.
    TimeOffset rescaled(Ppqn from, Ppqn to) const;
    std::chrono::nanoseconds at(const TempoMap& tempo, Tick tick) const;
    Tick undo(const TempoMap& tempo, std::chrono::nanoseconds time) const;

    friend bool operator==(const TimeOffset&, const TimeOffset&) = default;
    friend std::string offsetText(const TimeOffset& offset);

private:
    struct Coarse
    {
        NoteFraction fraction;
        bool earlier;
        Tick fine;

        friend bool operator==(const Coarse&, const Coarse&) = default;
    };

    std::variant<std::chrono::milliseconds, Tick> value_{std::chrono::milliseconds{0}};
    std::optional<Coarse> coarse_; // how a musical offset was given, unless as plain ticks
};

// Explicit units: -40ms, +12t, +1/16, +1/16-22t (including dotted and triplet fractions).
std::optional<TimeOffset> parseTimeOffset(std::string_view text, Ppqn ppqn);
// As parseTimeOffset reads it, and as it was given.
std::string offsetText(const TimeOffset& offset);

} // namespace modal::core
