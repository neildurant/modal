// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/base16.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace modal::ui
{

namespace
{

std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// A value as YAML writes it: in quotes, or bare up to a comment.
std::string_view valueOf(std::string_view text)
{
    text = trimmed(text);
    if (!text.empty() && (text.front() == '"' || text.front() == '\''))
    {
        const auto end = text.find(text.front(), 1);
        return end == std::string_view::npos ? text.substr(1) : text.substr(1, end - 1);
    }
    if (const auto comment = text.find(" #"); comment != std::string_view::npos)
    {
        text = text.substr(0, comment);
    }
    return trimmed(text);
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "#rrggbb" or "rrggbb", in either case.
std::optional<Colour> colourOf(std::string_view text)
{
    if (text.starts_with('#'))
    {
        text.remove_prefix(1);
    }
    if (text.size() != 6)
    {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto high = hexDigit(text[2 * i]);
        const auto low = hexDigit(text[2 * i + 1]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        parts[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Colour{.r = parts[0], .g = parts[1], .b = parts[2]};
}

// Which colour a key names, base00 to base17, as its number, or none.
std::optional<std::size_t> slotOf(std::string_view key)
{
    if (key.size() != 6 || !key.starts_with("base"))
    {
        return std::nullopt;
    }
    const auto high = hexDigit(key[4]);
    const auto low = hexDigit(key[5]);
    if (high < 0 || high > 1 || low < 0)
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(high * 16 + low);
}

// A colour part of the way from one to another.
Colour blend(Colour from, Colour to, double part)
{
    const auto mix = [&](std::uint8_t a, std::uint8_t b)
    { return static_cast<std::uint8_t>(a + (b - a) * part + 0.5); };
    return {.r = mix(from.r, to.r), .g = mix(from.g, to.g), .b = mix(from.b, to.b)};
}

double luminance(Colour colour)
{
    const auto linear = [](std::uint8_t part)
    {
        const auto c = part / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(colour.r) + 0.7152 * linear(colour.g) + 0.0722 * linear(colour.b);
}

// The first colour on the way from one to another, in twentieths, that stands apart from
// each of some backgrounds by at least so much; at most so far along.
Colour standingApart(Colour from, Colour to, std::initializer_list<Colour> backgrounds,
                     double least, double furthest)
{
    for (int step = 0;; ++step)
    {
        const auto part = std::min(step / 20.0, furthest);
        const auto colour = blend(from, to, part);
        if (part >= furthest ||
            std::ranges::all_of(backgrounds, [&](Colour background)
                                { return contrast(colour, background) >= least; }))
        {
            return colour;
        }
    }
}

} // namespace

double contrast(Colour a, Colour b)
{
    // As values: std::minmax of temporaries would give references to them, gone at once.
    const auto first = luminance(a);
    const auto second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

Base16Read readBase16(std::string_view text)
{
    Base16Scheme scheme;
    std::array<std::optional<Colour>, 24> colours;
    int number = 0;
    while (!text.empty())
    {
        ++number;
        const auto end = text.find('\n');
        const auto line = trimmed(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos)
        {
            return {.scheme = std::nullopt,
                    .error = "line " + std::to_string(number) + " isn't a name and a value"};
        }
        const auto key = trimmed(line.substr(0, colon));
        const auto value = valueOf(line.substr(colon + 1));
        if (key == "name" || key == "scheme")
        {
            scheme.name = value;
        }
        else if (key == "author")
        {
            scheme.author = value;
        }
        else if (const auto slot = slotOf(key); slot && *slot < colours.size())
        {
            const auto colour = colourOf(value);
            if (!colour)
            {
                return {.scheme = std::nullopt,
                        .error = "line " + std::to_string(number) + ": " + std::string{key} +
                                 " should be a colour, as \"#rrggbb\", not \"" +
                                 std::string{value} + "\""};
            }
            colours[*slot] = colour;
        }
        // Anything else, such as system, variant, slug or palette:, says nothing a theme needs.
    }
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (!colours[i])
        {
            static constexpr std::string_view kDigits = "0123456789ABCDEF";
            return {.scheme = std::nullopt,
                    .error = std::string{"it has no base0"} + kDigits[i] +
                             ", so it isn't a base16 scheme"};
        }
    }
    // base24's eight more, if it has them all.
    bool base24 = true;
    for (std::size_t i = 16; i < 24; ++i)
    {
        base24 = base24 && colours[i];
    }
    for (std::size_t i = 0; i < (base24 ? 24 : 16); ++i)
    {
        scheme.colours.push_back(*colours[i]);
    }
    return {.scheme = std::move(scheme), .error = {}};
}

std::string base16ThemeText(const Base16Scheme& scheme)
{
    // After the base16 styling guide: base00 the background, base01 and base02 lighter ones
    // (bars, then selections), base03 comments, base04 and base05 the foregrounds; then red,
    // orange, yellow, green, cyan, blue, magenta and brown. base24's brighter red and cyan,
    // base12 and base15, are for what must stand out most.
    const auto& c = scheme.colours;
    const auto base = [&](std::size_t i) { return c[i]; };
    const bool base24 = c.size() >= 24;
    const auto background = base(0x00);
    const auto red = base(0x08);
    const auto orange = base(0x09);
    const auto yellow = base(0x0A);
    const auto green = base(0x0B);
    const auto cyan = base(0x0C);
    const auto blue = base(0x0D);
    const auto magenta = base(0x0E);
    const auto brightRed = base24 ? base(0x12) : red;
    const auto brightCyan = base24 ? base(0x15) : cyan;
    // Readable whatever the scheme (decided 24 November 2026, after trial, the comment colour
    // being too dim as secondary text): the panel, base02, only as far towards the background
    // as text needs to read on it; dim text from the comment colour towards the text's as far
    // as it needs to read on the background and the panel, staying dimmer than text.
    const auto foreground = base(0x05);
    const auto panel = standingApart(base(0x02), background, {foreground}, 4.5, 1.0);
    const auto dim = standingApart(base(0x03), foreground, {background, panel}, 3.0, 0.75);

    const std::array<std::pair<Role, Colour>, kRoleCount> roles{{
        {Role::Background, background},
        {Role::Text, foreground},
        {Role::Dim, dim},
        {Role::Accent, blue},
        {Role::Warning, red},
        {Role::Message, yellow},
        {Role::Cursor, yellow},
        {Role::CursorText, background},
        {Role::Selection, blend(base(0x02), blue, 0.25)},
        {Role::Playhead, blend(background, green, 0.2)},
        {Role::Bar, base(0x01)},
        {Role::BarText, base(0x04)},
        {Role::Panel, panel},
        {Role::Chrome, base(0x01)},
        {Role::Mode, blue},
        {Role::ModeText, background},
        {Role::GridLine, base(0x01)},
        {Role::BeatLine, base(0x02)},
        {Role::BarLine, base(0x03)},
        {Role::Note, green},
        {Role::Label, orange},
        {Role::Density1, blend(background, green, 0.25)},
        {Role::Density2, blend(background, green, 0.5)},
        {Role::Density3, blend(background, green, 0.75)},
        {Role::Density4, green},
        {Role::OwnMeter, magenta},
        {Role::TrackRows, base(0x01)},
        {Role::HintKey, green},
        {Role::HintGroup, magenta},
        {Role::NoteSelected, orange},
        {Role::Match, brightCyan},
        {Role::TrackMute, blend(red, base(0x05), 0.35)},
        {Role::TrackSolo, green},
        {Role::TrackChase, blue},
        {Role::TrackQuantize, cyan},
        {Role::TrackArm, brightRed},
        {Role::TrackThru, yellow},
        {Role::ToggleText, background},
        {Role::EventSign, magenta},
        {Role::Field, blend(background, base(0x01), 0.5)},
        {Role::Indicator, blend(base(0x01), base(0x02), 0.5)},
        {Role::IndicatorText, base(0x04)},
        {Role::IndicatorLitText, background},
        {Role::IndicatorArm, brightRed},
        {Role::IndicatorRecordMode, orange},
        {Role::IndicatorCount, yellow},
        {Role::IndicatorClick, blue},
        {Role::IndicatorThru, blue},
        {Role::IndicatorAudition, blue},
        {Role::IndicatorRewind, blue},
        {Role::IndicatorInput, green},
        {Role::MessageLine, base(0x01)},
        {Role::Take1, blue},
        {Role::Take2, magenta},
        {Role::Take3, cyan},
        {Role::Take4, green},
    }};

    std::string text = "\" " + (scheme.name.empty() ? std::string{"A base16 scheme"} : scheme.name);
    if (!scheme.author.empty())
    {
        text += ", by " + scheme.author;
    }
    text += ", as a Modal theme.\n";
    for (const auto& [role, colour] : roles)
    {
        text += "highlight " + std::string{roleName(role)} + " " + formatColour(colour) + "\n";
    }
    return text;
}

} // namespace modal::ui
