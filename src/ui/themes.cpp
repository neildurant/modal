// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/base16.hpp>
#include <modal/ui/themes.hpp>

#include <array>
#include <cassert>
#include <cstddef>

namespace modal::ui
{

// Built in from assets/themes by modal_embed_file.
std::span<const unsigned char> builtInDarkTheme();
std::span<const unsigned char> builtInLightTheme();
// And the base16 schemes, as src/ui/CMakeLists.txt lists them.
std::span<const unsigned char> builtInScheme0();
std::span<const unsigned char> builtInScheme1();
std::span<const unsigned char> builtInScheme2();
std::span<const unsigned char> builtInScheme3();
std::span<const unsigned char> builtInScheme4();
std::span<const unsigned char> builtInScheme5();
std::span<const unsigned char> builtInScheme6();
std::span<const unsigned char> builtInScheme7();

namespace
{

constexpr std::array<std::string_view, 10> kThemeNames{
    "dark",    "light",          "gruvbox-dark-medium", "gruvbox-light-medium", "nord",
    "dracula", "solarized-dark", "solarized-light",     "catppuccin-mocha",     "catppuccin-latte"};

constexpr std::array kSchemes{&builtInScheme0, &builtInScheme1, &builtInScheme2, &builtInScheme3,
                              &builtInScheme4, &builtInScheme5, &builtInScheme6, &builtInScheme7};
static_assert(kSchemes.size() + 2 == kThemeNames.size());

std::string_view text(std::span<const unsigned char> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

} // namespace

std::span<const std::string_view> builtInThemeNames()
{
    return kThemeNames;
}

std::optional<std::string> builtInThemeText(std::string_view name)
{
    if (name == "dark")
    {
        return std::string{text(builtInDarkTheme())};
    }
    if (name == "light")
    {
        return std::string{text(builtInLightTheme())};
    }
    for (std::size_t i = 0; i < kSchemes.size(); ++i)
    {
        if (name == kThemeNames[i + 2])
        {
            const auto read = readBase16(text(kSchemes[i]()));
            assert(read.scheme); // the tests read every built-in scheme
            return read.scheme ? std::optional{base16ThemeText(*read.scheme)} : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<Theme> builtInTheme(std::string_view name)
{
    const auto file = builtInThemeText(name);
    if (!file)
    {
        return std::nullopt;
    }
    // The built-in themes hold only comments and highlight lines, so they're read here
    // rather than by ConfigReader, which a Config's default theme comes before.
    Theme theme;
    std::string_view rest = *file;
    while (!rest.empty())
    {
        const auto end = rest.find('\n');
        const auto line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);

        std::vector<std::string> words;
        for (std::size_t i = 0; i < line.size();)
        {
            if (isSpace(line[i]))
            {
                ++i;
                continue;
            }
            const auto start = i;
            while (i < line.size() && !isSpace(line[i]))
            {
                ++i;
            }
            words.emplace_back(line.substr(start, i - start));
        }
        if (words.empty() || words.front().starts_with('"'))
        {
            continue;
        }
        assert(words.front() == "highlight");
        [[maybe_unused]] const auto error = highlight(theme, {words.begin() + 1, words.end()});
        assert(error.empty()); // the tests read every built-in theme
    }
    return theme;
}

std::string highlight(Theme& theme, const std::vector<std::string>& arguments)
{
    if (arguments.size() != 2)
    {
        return "highlight needs ROLE #RRGGBB";
    }
    const auto role = roleFromName(arguments[0]);
    if (!role)
    {
        return "unknown role \"" + arguments[0] + "\"";
    }
    const auto colour = parseColour(arguments[1]);
    if (!colour)
    {
        return "a colour is written #rrggbb, not " + arguments[1];
    }
    theme.set(*role, *colour);
    return {};
}

} // namespace modal::ui
