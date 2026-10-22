// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/note_fraction.hpp>

#include <bit>
#include <charconv>
#include <cstdint>
#include <system_error>

namespace modal::core
{

namespace
{

// Upper limit for each part of a parsed fraction, so later arithmetic can't overflow.
constexpr std::int64_t kMaxPart = std::int64_t{1} << 20;

std::optional<std::int64_t> parsePart(std::string_view text)
{
    std::int64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || ptr != end || value <= 0 || value > kMaxPart)
    {
        return std::nullopt;
    }
    return value;
}

bool isPowerOfTwo(std::int64_t value)
{
    return value > 0 && std::has_single_bit(static_cast<std::uint64_t>(value));
}

} // namespace

std::optional<NoteFraction> parseNoteFraction(std::string_view text)
{
    enum class Modifier
    {
        None,
        Dotted,
        DoubleDotted,
        Triplet,
    };

    auto modifier = Modifier::None;
    if (text.ends_with(".."))
    {
        modifier = Modifier::DoubleDotted;
        text.remove_suffix(2);
    }
    else if (text.ends_with('.'))
    {
        modifier = Modifier::Dotted;
        text.remove_suffix(1);
    }
    else if (text.ends_with('t'))
    {
        modifier = Modifier::Triplet;
        text.remove_suffix(1);
    }

    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto numerator = parsePart(text.substr(0, slash));
    const auto denominator = parsePart(text.substr(slash + 1));
    if (!numerator || !denominator)
    {
        return std::nullopt;
    }

    const NoteFraction plain{*numerator, *denominator};
    switch (modifier)
    {
    case Modifier::None:
        return plain;
    case Modifier::Dotted:
        return plain.dotted();
    case Modifier::DoubleDotted:
        return NoteFraction{plain.numerator() * 7, plain.denominator() * 4};
    case Modifier::Triplet:
        return plain.triplet();
    }
    return std::nullopt;
}

std::string toString(NoteFraction fraction)
{
    const auto numerator = fraction.numerator();
    const auto denominator = fraction.denominator();

    // Triplets of power-of-two notes: 1/12 is an eighth-note triplet.
    if (numerator == 1 && denominator % 3 == 0 && isPowerOfTwo(denominator / 3 * 2))
    {
        return "1/" + std::to_string(denominator / 3 * 2) + "t";
    }
    // Dotted and double-dotted power-of-two notes: 3/16 is a dotted eighth.
    if (numerator == 3 && denominator >= 2 && isPowerOfTwo(denominator))
    {
        return "1/" + std::to_string(denominator / 2) + ".";
    }
    if (numerator == 7 && denominator >= 4 && isPowerOfTwo(denominator))
    {
        return "1/" + std::to_string(denominator / 4) + "..";
    }
    return std::to_string(numerator) + "/" + std::to_string(denominator);
}

} // namespace modal::core
