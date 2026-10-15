#pragma once

#include <modal/core/tick.hpp>

#include <cassert>
#include <compare>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>

namespace modal::core
{

// A musical length as a fraction of a whole note: 1/4 is a quarter note, 3/16 a dotted
// eighth, 1/12 an eighth-note triplet. Grid steps, step-entry lengths, quantise values and
// configuration settings are stored like this and converted to ticks only when used.
// Always positive and kept in lowest terms.
class NoteFraction
{
public:
    constexpr NoteFraction(std::int64_t numerator, std::int64_t denominator)
        : numerator_(numerator)
        , denominator_(denominator)
    {
        assert(numerator > 0 && denominator > 0);
        const auto divisor = std::gcd(numerator_, denominator_);
        numerator_ /= divisor;
        denominator_ /= divisor;
    }

    constexpr std::int64_t numerator() const { return numerator_; }
    constexpr std::int64_t denominator() const { return denominator_; }

    // One and a half times as long, e.g. 1/8 -> 1/8. (3/16).
    constexpr NoteFraction dotted() const { return {numerator_ * 3, denominator_ * 2}; }

    // Two thirds as long, e.g. 1/8 -> 1/8t (1/12).
    constexpr NoteFraction triplet() const { return {numerator_ * 2, denominator_ * 3}; }

    // The length in ticks at the given resolution, rounded to the nearest tick when the
    // fraction doesn't divide evenly (halves round up).
    constexpr Tick toTicks(Ppqn ppqn) const
    {
        const Tick scaled = ppqn.whole() * numerator_;
        return (2 * scaled + denominator_) / (2 * denominator_);
    }

    // Whether toTicks() is exact at the given resolution.
    constexpr bool isExact(Ppqn ppqn) const
    {
        return ppqn.whole() * numerator_ % denominator_ == 0;
    }

    friend constexpr bool operator==(NoteFraction, NoteFraction) = default;

    friend constexpr std::strong_ordering operator<=>(NoteFraction a, NoteFraction b)
    {
        return a.numerator_ * b.denominator_ <=> b.numerator_ * a.denominator_;
    }

private:
    std::int64_t numerator_;
    std::int64_t denominator_;
};

// Parses "n/d", optionally followed by "." (dotted), ".." (double dotted) or "t" (triplet),
// e.g. "1/16", "3/8", "1/8.", "1/4..", "1/16t". Returns nothing for anything else,
// including zero or out-of-range numbers.
std::optional<NoteFraction> parseNoteFraction(std::string_view text);

// Formats a fraction so that parseNoteFraction() reads it back, preferring the musical
// spelling: 1/12 -> "1/8t", 3/16 -> "1/8.", 7/16 -> "1/4..", 5/8 -> "5/8".
std::string toString(NoteFraction fraction);

} // namespace modal::core
