// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/time_offset.hpp>

#include <charconv>

namespace modal::core
{
namespace
{
std::optional<std::int64_t> integer(std::string_view text)
{
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
        if (text.starts_with('-') || text.starts_with('+'))
        {
            return std::nullopt;
        }
    }
    if (text.empty())
    {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}
} // namespace

TimeOffset TimeOffset::ticks(Tick value)
{
    TimeOffset result;
    result.value_ = value;
    return result;
}

std::int64_t TimeOffset::amount() const
{
    return musical() ? std::get<Tick>(value_) : std::get<std::chrono::milliseconds>(value_).count();
}

bool TimeOffset::valid(Ppqn ppqn) const
{
    const auto limit = musical() ? kMaxMusical.toTicks(ppqn) : kMaxMilliseconds.count();
    return amount() >= -limit && amount() <= limit;
}

std::chrono::nanoseconds TimeOffset::at(const TempoMap& tempo, Tick tick) const
{
    if (!musical())
    {
        return tempo.toTime(tick) + std::get<std::chrono::milliseconds>(value_);
    }
    const auto shifted = tick + amount();
    return tempo.toTimeExtended(shifted);
}

Tick TimeOffset::undo(const TempoMap& tempo, std::chrono::nanoseconds time) const
{
    return musical() ? tempo.toTickExtended(time) - amount()
                     : tempo.toTickExtended(time - std::get<std::chrono::milliseconds>(value_));
}

std::optional<TimeOffset> parseTimeOffset(std::string_view text, Ppqn ppqn)
{
    if (text.empty())
    {
        return std::nullopt;
    }
    if (text.ends_with("ms"))
    {
        const auto value = integer(text.substr(0, text.size() - 2));
        if (!value)
        {
            return std::nullopt;
        }
        const TimeOffset result{std::chrono::milliseconds{*value}};
        return result.valid(ppqn) ? std::optional{result} : std::nullopt;
    }
    if (text.find('/') == std::string_view::npos)
    {
        if (!text.ends_with('t'))
        {
            return std::nullopt;
        }
        const auto value = integer(text.substr(0, text.size() - 1));
        if (!value)
        {
            return std::nullopt;
        }
        const auto result = TimeOffset::ticks(*value);
        return result.valid(ppqn) ? std::optional{result} : std::nullopt;
    }
    const int sign = text.front() == '-' ? -1 : 1;
    if (text.front() == '-' || text.front() == '+')
    {
        text.remove_prefix(1);
    }
    const auto split = text.find_first_of("+-");
    const auto fraction = parseNoteFraction(text.substr(0, split));
    if (!fraction || *fraction > TimeOffset::kMaxMusical)
    {
        return std::nullopt;
    }
    auto value = sign * fraction->toTicks(ppqn);
    if (split != std::string_view::npos)
    {
        const auto fineText = text.substr(split);
        if (!fineText.ends_with('t'))
        {
            return std::nullopt;
        }
        const auto fine = integer(fineText.substr(0, fineText.size() - 1));
        const auto limit = TimeOffset::kMaxMusical.toTicks(ppqn);
        if (!fine || *fine < -limit || *fine > limit)
        {
            return std::nullopt;
        }
        value += *fine;
    }
    const auto result = TimeOffset::ticks(value);
    return result.valid(ppqn) ? std::optional{result} : std::nullopt;
}

std::string offsetText(TimeOffset offset)
{
    return (offset.amount() > 0 ? "+" : "") + std::to_string(offset.amount()) +
           (offset.musical() ? "t" : "ms");
}
} // namespace modal::core
