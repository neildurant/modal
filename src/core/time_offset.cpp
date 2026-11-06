// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/time_offset.hpp>

#include <algorithm>
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

TimeOffset TimeOffset::fraction(NoteFraction coarse, bool earlier, Tick fine, Ppqn ppqn)
{
    auto result = ticks((earlier ? -1 : 1) * coarse.toTicks(ppqn) + fine);
    result.coarse_ = Coarse{coarse, earlier, fine};
    return result;
}

TimeOffset TimeOffset::nudged(std::int64_t by) const
{
    auto result = *this;
    if (!musical())
    {
        result.value_ = std::get<std::chrono::milliseconds>(value_) + std::chrono::milliseconds{by};
        return result;
    }
    result.value_ = std::get<Tick>(value_) + by;
    if (result.coarse_)
    {
        result.coarse_->fine += by;
    }
    return result;
}

TimeOffset TimeOffset::stepped(NoteFraction step, int steps, Ppqn ppqn) const
{
    if (!coarse_)
    {
        return musical() ? ticks(amount() + steps * std::max<Tick>(1, step.toTicks(ppqn))) : *this;
    }
    // The signed sum of two fractions, over a common denominator.
    const auto denominator = coarse_->fraction.denominator() * step.denominator();
    const auto sum =
        (coarse_->earlier ? -1 : 1) * coarse_->fraction.numerator() * step.denominator() +
        steps * step.numerator() * coarse_->fraction.denominator();
    if (sum == 0)
    {
        return ticks(coarse_->fine);
    }
    return fraction({sum < 0 ? -sum : sum, denominator}, sum < 0, coarse_->fine, ppqn);
}

TimeOffset TimeOffset::rescaled(Ppqn from, Ppqn to) const
{
    if (!musical())
    {
        return *this;
    }
    const auto scale = [&](Tick value)
    {
        const auto magnitude = value < 0 ? -value : value;
        const auto scaled =
            magnitude / from.value * to.value +
            (2 * (magnitude % from.value) * to.value + from.value) / (2 * from.value);
        return value < 0 ? -scaled : scaled;
    };
    return coarse_ ? fraction(coarse_->fraction, coarse_->earlier, scale(coarse_->fine), to)
                   : ticks(scale(amount()));
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
    const bool earlier = text.front() == '-';
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
    Tick fineTicks = 0;
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
        fineTicks = *fine;
    }
    const auto result = TimeOffset::fraction(*fraction, earlier, fineTicks, ppqn);
    return result.valid(ppqn) ? std::optional{result} : std::nullopt;
}

std::string offsetText(const TimeOffset& offset)
{
    if (offset.coarse_)
    {
        const auto fine = offset.coarse_->fine;
        return (offset.coarse_->earlier ? "-" : "+") + toString(offset.coarse_->fraction) +
               (fine == 0 ? "" : (fine > 0 ? "+" : "") + std::to_string(fine) + "t");
    }
    return (offset.amount() > 0 ? "+" : "") + std::to_string(offset.amount()) +
           (offset.musical() ? "t" : "ms");
}
} // namespace modal::core
