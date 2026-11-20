// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/track_import.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>

namespace modal::ui
{

namespace
{

std::string lower(std::string_view text)
{
    std::string result;
    for (const char c : text)
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::optional<std::size_t> number(std::string_view text)
{
    std::size_t value = 0;
    const auto* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || stop != end)
        return std::nullopt;
    return value;
}

} // namespace

std::variant<std::vector<std::size_t>, std::string> parseTrackSelection(std::string_view text,
                                                                        std::size_t count)
{
    const auto words = lower(trimmed(text));
    std::vector<std::size_t> chosen;
    if (words == "all")
    {
        for (std::size_t i = 0; i < count; ++i)
            chosen.push_back(i);
        return chosen;
    }
    if (words.empty() || words == "none")
        return chosen;
    const auto missing = [count](std::size_t n)
    { return "No track " + std::to_string(n) + ": the file has " + std::to_string(count); };
    std::string_view rest = words;
    while (!rest.empty())
    {
        const auto end = rest.find_first_of(", \t");
        const auto part = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (part.empty())
            continue;
        const auto dash = part.find('-');
        const auto from = number(part.substr(0, dash));
        const auto to = dash == std::string_view::npos ? from : number(part.substr(dash + 1));
        if (!from || !to)
            return "Not a track number or range: " + std::string{part};
        if (*from > *to)
            return std::string{part} + " runs backwards: write " + std::to_string(*to) + "-" +
                   std::to_string(*from);
        for (const auto n : {*from, *to})
        {
            if (n == 0 || n > count)
                return missing(n);
        }
        for (auto n = *from; n <= *to; ++n)
            chosen.push_back(n - 1);
    }
    std::ranges::sort(chosen);
    chosen.erase(std::ranges::unique(chosen).begin(), chosen.end());
    return chosen;
}

bool looksLikeTrackSelection(std::string_view text)
{
    const auto words = lower(trimmed(text));
    if (words == "all" || words == "none")
        return true;
    return !words.empty() &&
           std::ranges::any_of(words, [](char c) { return c >= '0' && c <= '9'; }) &&
           std::ranges::all_of(
               words,
               [](char c) { return (c >= '0' && c <= '9') || c == ',' || c == '-' || c == ' '; });
}

} // namespace modal::ui
