// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/remembered.hpp>

#include <algorithm>
#include <charconv>
#include <optional>

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
    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto top = number(text.substr(0, slash));
    const auto bottom = number(text.substr(slash + 1));
    if (!top || !bottom)
    {
        return std::nullopt;
    }
    const core::TimeSignature meter{static_cast<std::int32_t>(*top),
                                    static_cast<std::int32_t>(*bottom)};
    return meter.isValid() ? std::optional{meter} : std::nullopt;
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
