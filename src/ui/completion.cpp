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

} // namespace

std::vector<std::string> matching(std::span<const std::string> choices, std::string_view typed)
{
    std::vector<std::string> found;
    for (const auto& choice : choices)
    {
        if (startsWith(choice, typed))
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
    // The words, as runs of what isn't a space; the cursor in one, or touching its end, or else
    // an empty word where it is, after those before it.
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
        while (end < line.size() && !space(line[end]))
        {
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

std::vector<std::string> fileChoices(const std::filesystem::path& folder, std::string_view word)
{
    const auto slash = word.find_last_of('/');
    const auto in =
        slash == std::string_view::npos ? std::string{} : std::string{word.substr(0, slash + 1)};
    const auto name = slash == std::string_view::npos ? word : word.substr(slash + 1);
    std::vector<std::string> found;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator{folder / utf8Path(in), error})
    {
        auto file = utf8Text(entry.path().filename());
        if (file.starts_with('.') && !name.starts_with('.'))
        {
            continue;
        }
        found.push_back(in + file + (entry.is_directory(error) ? "/" : ""));
    }
    std::ranges::sort(found);
    return found;
}

std::vector<std::string> settingChoices(const Settings& settings, std::string_view word,
                                        std::span<const std::string> themes)
{
    std::vector<std::string> found;
    const auto equals = word.find('=');
    if (equals == std::string_view::npos)
    {
        for (const auto& setting : settings.all())
        {
            const std::string name{setting.name};
            if (std::holds_alternative<bool>(setting.value))
            {
                found.push_back(name);
                found.push_back("no" + name);
            }
            else
            {
                found.push_back(name + "=");
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
    { return std::string{name} + "=" + std::string{value}; };
    if (name == "theme")
    {
        for (const auto& theme : themes)
        {
            found.push_back(with(theme));
        }
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
