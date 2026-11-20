// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/recent_songs.hpp>

#include <array>
#include <exception>

namespace modal::ui
{

namespace
{

std::string twoDigits(long value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

} // namespace

std::string usedText(std::chrono::local_seconds used, std::chrono::local_seconds now)
{
    using namespace std::chrono;
    static constexpr std::array<const char*, 12> kMonths{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static constexpr std::array<const char*, 7> kDays{"Sun", "Mon", "Tue", "Wed",
                                                      "Thu", "Fri", "Sat"};
    const auto day = floor<days>(used);
    const auto today = floor<days>(now);
    const hh_mm_ss clock{floor<minutes>(used - day)};
    const auto time = twoDigits(clock.hours().count()) + ":" + twoDigits(clock.minutes().count());
    if (day == today)
        return "today " + time;
    if (day == today - days{1})
        return "yesterday " + time;
    const year_month_day date{day};
    const year_month_day current{today};
    const auto dateText = std::to_string(static_cast<unsigned>(date.day())) + " " +
                          kMonths[static_cast<unsigned>(date.month()) - 1];
    if (date.year() == current.year())
        return std::string{kDays[weekday{day}.c_encoding()]} + " " + dateText;
    return dateText + " " + std::to_string(static_cast<int>(date.year()));
}

std::string usedText(std::int64_t used, std::int64_t now)
{
    using namespace std::chrono;
    const sys_seconds usedAt{seconds{used}};
    const sys_seconds nowAt{seconds{now}};
    try
    {
        const auto* zone = current_zone();
        return usedText(zone->to_local(usedAt), zone->to_local(nowAt));
    }
    catch (const std::exception&)
    {
        // No time zone database: UTC, rather than nothing.
        return usedText(local_seconds{usedAt.time_since_epoch()},
                        local_seconds{nowAt.time_since_epoch()});
    }
}

} // namespace modal::ui
