// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/palette.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <tuple>

namespace modal::ui
{

namespace
{

std::string lowered(std::string_view text)
{
    std::string result{text};
    for (auto& c : result)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool startsWord(std::string_view text, std::string_view query)
{
    // At the start of a word: after a space, a hyphen, an underscore or a slash.
    for (std::size_t at = text.find(query); at != std::string_view::npos;
         at = text.find(query, at + 1))
    {
        if (at == 0 || std::string_view{" -_/"}.find(text[at - 1]) != std::string_view::npos)
        {
            return true;
        }
    }
    return false;
}

bool inOrder(std::string_view text, std::string_view query)
{
    std::size_t at = 0;
    for (const char c : query)
    {
        if (c == ' ')
        {
            continue;
        }
        at = text.find(c, at);
        if (at == std::string_view::npos)
        {
            return false;
        }
        ++at;
    }
    return true;
}

std::optional<int> tier(const PaletteItem& item, std::string_view query)
{
    const auto title = lowered(item.title);
    const auto name = lowered(item.name);
    if (title.starts_with(query))
    {
        return 0;
    }
    if (startsWord(title, query) || name.starts_with(query))
    {
        return 1;
    }
    if (inOrder(title, query))
    {
        return 2;
    }
    if (lowered(item.description).find(query) != std::string::npos ||
        lowered(item.aliases).find(query) != std::string::npos ||
        name.find(query) != std::string::npos)
    {
        return 3;
    }
    return std::nullopt;
}

} // namespace

std::vector<std::size_t> paletteMatches(std::span<const PaletteItem> items, std::string_view query)
{
    const auto wanted = lowered(query);
    std::vector<std::tuple<int, std::string, std::size_t>> found;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto rank = wanted.empty() ? std::optional{0} : tier(items[i], wanted);
        if (rank)
        {
            found.emplace_back(*rank + (items[i].offered ? 0 : 4), lowered(items[i].title), i);
        }
    }
    std::ranges::sort(found);
    std::vector<std::size_t> result;
    for (const auto& [rank, title, index] : found)
    {
        result.push_back(index);
    }
    return result;
}

} // namespace modal::ui
