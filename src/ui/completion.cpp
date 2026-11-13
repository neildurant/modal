// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <variant>

namespace modal::ui
{

namespace
{

char folded(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool startsWith(std::string_view text, std::string_view start)
{
    return text.size() >= start.size() &&
           std::ranges::equal(text.substr(0, start.size()), start,
                              [](char a, char b) { return folded(a) == folded(b); });
}

bool separator(char c)
{
    return c == '-' || c == '_' || c == ' ' || c == '/' || c == '.';
}

// A choice or a word typed, matched without the quote it may start with.
std::string_view unquoted(std::string_view text)
{
    return text.starts_with('"') ? text.substr(1) : text;
}

} // namespace

std::vector<std::string> matching(std::span<const std::string> choices, std::string_view typed)
{
    std::vector<std::string> found;
    typed = unquoted(typed);
    for (const auto& choice : choices)
    {
        if (startsWith(unquoted(choice), typed))
        {
            found.push_back(choice);
        }
    }
    if (!found.empty() || typed.empty())
    {
        return found;
    }
    for (const auto& choice : choices)
    {
        for (std::size_t i = 1; i < choice.size(); ++i)
        {
            if (separator(choice[i - 1]) && startsWith(std::string_view{choice}.substr(i), typed))
            {
                found.push_back(choice);
                break;
            }
        }
    }
    return found;
}

std::string sharedStart(std::span<const std::string> choices)
{
    if (choices.empty())
    {
        return {};
    }
    std::size_t length = choices.front().size();
    for (const auto& choice : choices.subspan(1))
    {
        std::size_t same = 0;
        while (same < length && same < choice.size() &&
               folded(choice[same]) == folded(choices.front()[same]))
        {
            ++same;
        }
        length = same;
    }
    return choices.front().substr(0, length);
}

LineWord wordAt(std::u32string_view line, std::size_t cursor)
{
    // The words, as runs of what isn't a space, but for spaces in quotes, as in a path with
    // spaces; the cursor in one, or touching its end, or else an empty word where it is, after
    // those before it.
    cursor = std::min(cursor, line.size());
    const auto space = [](char32_t c) { return c == U' ' || c == U'\t'; };
    std::size_t index = 0;
    for (std::size_t at = 0; at < line.size();)
    {
        if (space(line[at]))
        {
            ++at;
            continue;
        }
        std::size_t end = at;
        bool quoted = false;
        while (end < line.size() && (quoted || !space(line[end])))
        {
            quoted = quoted != (line[end] == U'"');
            ++end;
        }
        if (at <= cursor && cursor <= end)
        {
            return {at, end, index};
        }
        if (end > cursor)
        {
            break;
        }
        ++index;
        at = end;
    }
    return {cursor, cursor, index};
}

std::vector<std::string> fileChoices(const std::filesystem::path& folder, std::string_view word,
                                     const std::optional<std::filesystem::path>& home)
{
    const bool quoted = word.starts_with('"');
    const auto text = unquoted(word);
    const auto slash = std::ranges::find_if(text.rbegin(), text.rend(), pathSeparator);
    const auto in = std::string{text.substr(0, static_cast<std::size_t>(text.rend() - slash))};
    const auto name = std::string_view{text}.substr(in.size());
    // Folders end with the separator typed last, or else /, which works everywhere.
    const char sep = in.empty() ? '/' : in.back();
    std::vector<std::string> found;
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator{typedPath(in.empty() ? "." : in, folder, home), error})
    {
        auto file = utf8Text(entry.path().filename());
        if (file.starts_with('.') && !name.starts_with('.'))
        {
            continue;
        }
        found.push_back(in + file + (entry.is_directory(error) ? std::string{sep} : ""));
    }
    std::ranges::sort(found);
    // In quotes, all of them alike, if the word is or any has a space: a folder's left open
    // to go on into.
    if (quoted ||
        std::ranges::any_of(found, [](const auto& f) { return f.find(' ') != std::string::npos; }))
    {
        for (auto& f : found)
        {
            f = '"' + f + (pathSeparator(f.back()) ? "" : "\"");
        }
    }
    return found;
}

std::vector<std::string> settingChoices(const Settings& settings, std::string_view word,
                                        std::span<const std::string> themes)
{
    std::vector<std::string> found;
    // Names alone, so ? or = can follow (decided November 2026); a name typed whole, and no
    // other's start, has its values offered, as if = were typed.
    const auto* exact = settings.find(word);
    const bool others =
        std::ranges::any_of(settings.all(), [&](const auto& setting)
                            { return setting.name.starts_with(word) && setting.name != word; });
    if (exact != nullptr && !others && !std::holds_alternative<bool>(exact->value))
    {
        return settingChoices(settings, std::string{word} + "=", themes);
    }
    const auto equals = word.find('=');
    if (equals == std::string_view::npos)
    {
        for (const auto& setting : settings.all())
        {
            const std::string name{setting.name};
            found.push_back(name);
            if (std::holds_alternative<bool>(setting.value))
            {
                found.push_back("no" + name);
            }
        }
        return found;
    }
    const auto name = word.substr(0, equals);
    const auto* setting = settings.find(name);
    if (setting == nullptr)
    {
        return found;
    }
    const auto with = [&](std::string_view value)
    { return std::string{name} + "=" + quotedArgument(value); };
    if (name == "theme")
    {
        for (const auto& theme : themes)
        {
            found.push_back(with(theme));
        }
    }
    else if (name == "timingunit")
    {
        for (const auto value : {"grid", "1/4", "1/8", "1/16", "1/32", "1/8t", "1/16t"})
            found.push_back(with(value));
    }
    else if (std::holds_alternative<bool>(setting->value))
    {
        found.push_back(with("on"));
        found.push_back(with("off"));
    }
    else
    {
        found.push_back(with(formatValue(setting->value)));
    }
    return found;
}

} // namespace modal::ui
