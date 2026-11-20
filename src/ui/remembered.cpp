// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>
#include <modal/ui/remembered.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>
#include <variant>

namespace modal::ui
{

namespace
{

constexpr std::int64_t kHour = 60 * 60;
constexpr std::int64_t kDay = 24 * kHour;

std::optional<std::int64_t> number(std::string_view text)
{
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<core::TimeSignature> meterOf(std::string_view text)
{
    return core::parseTimeSignature(text);
}

std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (true)
    {
        const auto start = line.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
        {
            return found;
        }
        const auto stop = std::min(line.find(' ', start), line.size());
        found.push_back(line.substr(start, stop - start));
        at = stop;
    }
}

} // namespace

void Remembered::ran(std::string_view command, std::int64_t now)
{
    const auto at = std::ranges::find(uses_, command, &Use::command);
    if (at == uses_.end())
    {
        uses_.push_back({std::string{command}, 1, now});
        return;
    }
    ++at->count;
    at->last = now;
}

double Remembered::rank(std::string_view command, std::int64_t now) const
{
    const auto at = std::ranges::find(uses_, command, &Use::command);
    if (at == uses_.end())
    {
        return 0;
    }
    const auto age = now - at->last;
    const double recent = age < kHour       ? 8
                          : age < kDay      ? 4
                          : age < 7 * kDay  ? 2
                          : age < 30 * kDay ? 1
                                            : 0.5;
    return static_cast<double>(at->count) * recent;
}

void Remembered::forgetCommands()
{
    uses_.clear();
}

void Remembered::typedMeter(core::TimeSignature meter)
{
    std::erase(meters_, meter);
    meters_.insert(meters_.begin(), meter);
    if (meters_.size() > kMaxMeters)
    {
        meters_.resize(kMaxMeters);
    }
}

void Remembered::usedLane(std::string lane)
{
    std::erase(lanes_, lane);
    lanes_.insert(lanes_.begin(), std::move(lane));
    if (lanes_.size() > kMaxLanes)
    {
        lanes_.resize(kMaxLanes);
    }
}

void Remembered::setMacro(char32_t name, std::vector<Key> keys)
{
    if (keys.empty())
    {
        macros_.erase(name);
    }
    else
    {
        macros_[name] = std::move(keys);
    }
}

const std::vector<Key>* Remembered::macro(char32_t name) const
{
    const auto it = macros_.find(name);
    return it != macros_.end() ? &it->second : nullptr;
}

void Remembered::usedSong(std::string path, std::int64_t now, std::size_t most)
{
    std::erase_if(songs_, [&](const RecentSong& song) { return song.path == path; });
    songs_.insert(songs_.begin(), RecentSong{std::move(path), now});
    if (songs_.size() > most)
        songs_.resize(most);
}

bool Remembered::forgetSong(std::string_view path)
{
    return std::erase_if(songs_, [&](const RecentSong& song) { return song.path == path; }) > 0;
}

std::string Remembered::text() const
{
    std::string text =
        "# What Modal remembers between sessions. Written by Modal; safe to delete.\n";
    for (const auto& use : uses_)
    {
        text += "ran " + use.command + " " + std::to_string(use.count) + " " +
                std::to_string(use.last) + "\n";
    }
    for (const auto& meter : meters_)
    {
        text += "meter " + std::to_string(meter.numerator) + "/" +
                std::to_string(meter.denominator) + "\n";
    }
    for (const auto& lane : lanes_)
    {
        text += "lane " + lane + "\n";
    }
    // In key notation, which has no spaces: "macro a 3>>j".
    for (const auto& [name, keys] : macros_)
    {
        text += "macro " + std::string(1, static_cast<char>(name)) + " " + formatKeys(keys) + "\n";
    }
    // The path last, as the rest of the line, as it may have spaces: "song 1763650000 /a b".
    for (const auto& song : songs_)
        text += "song " + std::to_string(song.used) + " " + song.path + "\n";
    return text;
}

Remembered Remembered::read(std::string_view text)
{
    Remembered remembered;
    std::size_t at = 0;
    while (at < text.size())
    {
        const auto end = std::min(text.find('\n', at), text.size());
        auto line = text.substr(at, end - at);
        at = end + 1;
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (line.starts_with("song "))
        {
            auto rest = line.substr(5);
            const auto space = rest.find(' ');
            const auto used = number(rest.substr(0, space));
            if (used && space != std::string_view::npos && space + 1 < rest.size() &&
                remembered.songs_.size() < kMaxSongs)
            {
                const auto path = rest.substr(space + 1);
                if (std::ranges::none_of(remembered.songs_,
                                         [&](const RecentSong& song) { return song.path == path; }))
                    remembered.songs_.push_back({std::string{path}, *used});
            }
            continue;
        }
        const auto parts = words(line);
        if (parts.size() == 4 && parts[0] == "ran")
        {
            const auto count = number(parts[2]);
            const auto last = number(parts[3]);
            if (count && last && *count > 0)
            {
                remembered.uses_.push_back({std::string{parts[1]}, *count, *last});
            }
        }
        else if (parts.size() == 3 && parts[0] == "macro" && parts[1].size() == 1 &&
                 parts[1][0] >= 'a' && parts[1][0] <= 'z')
        {
            if (auto keys = parseKeys(parts[2]); std::holds_alternative<std::vector<Key>>(keys))
            {
                remembered.setMacro(static_cast<char32_t>(parts[1][0]),
                                    std::move(std::get<std::vector<Key>>(keys)));
            }
        }
        else if (parts.size() == 2 && parts[0] == "lane")
        {
            if (remembered.lanes_.size() < kMaxLanes &&
                std::ranges::find(remembered.lanes_, parts[1]) == remembered.lanes_.end())
            {
                remembered.lanes_.emplace_back(parts[1]);
            }
        }
        else if (parts.size() == 2 && parts[0] == "meter")
        {
            if (const auto meter = meterOf(parts[1]);
                meter && remembered.meters_.size() < kMaxMeters &&
                std::ranges::find(remembered.meters_, *meter) == remembered.meters_.end())
            {
                remembered.meters_.push_back(*meter);
            }
        }
    }
    return remembered;
}

} // namespace modal::ui
