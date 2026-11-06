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

// A playback displacement, retaining whether it follows musical or elapsed time.
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

    bool musical() const { return std::holds_alternative<Tick>(value_); }
    std::int64_t amount() const;
    bool valid(Ppqn ppqn) const;
    std::chrono::nanoseconds at(const TempoMap& tempo, Tick tick) const;
    Tick undo(const TempoMap& tempo, std::chrono::nanoseconds time) const;

    friend bool operator==(const TimeOffset&, const TimeOffset&) = default;

private:
    std::variant<std::chrono::milliseconds, Tick> value_{std::chrono::milliseconds{0}};
};

// Explicit units: -40ms, +12t, +1/16, +1/16-22t (including dotted and triplet fractions).
std::optional<TimeOffset> parseTimeOffset(std::string_view text, Ppqn ppqn);
std::string offsetText(TimeOffset offset);

} // namespace modal::core
