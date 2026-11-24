// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/armed_range.hpp>

#include <algorithm>
#include <utility>

namespace modal::core
{

namespace
{

std::optional<std::size_t> sectionIndex(const Song& song, std::uint64_t id)
{
    const auto sections = song.sections();
    const auto it = std::ranges::find_if(sections, [id](const auto& s) { return s->id() == id; });
    if (it == sections.end())
        return std::nullopt;
    return static_cast<std::size_t>(it - sections.begin());
}

ArmedRange::LabelAt labelAt(const Song& song, std::size_t index)
{
    const auto& label = song.labels()[index];
    return {song.sections()[song.sectionAt(label.tick)]->id(), label.name};
}

std::optional<Tick> labelTick(const Song& song, const ArmedRange::LabelAt& at)
{
    const auto index = sectionIndex(song, at.section);
    if (!index)
        return std::nullopt;
    const auto labels = song.sections()[*index]->labels();
    const auto label = std::ranges::find(labels, at.name, &Label::name);
    if (label == labels.end())
        return std::nullopt;
    return song.sectionStart(*index) + label->tick;
}

ArmedRangeFound gone(std::string why)
{
    return {std::nullopt, std::move(why)};
}

} // namespace

ArmedRange ArmedRange::section(const Song& song, std::size_t index)
{
    return {OfSection{song.sections()[index]->id()}};
}

ArmedRange ArmedRange::labels(const Song& song, std::size_t from, std::size_t to)
{
    return {OfLabels{labelAt(song, from), labelAt(song, to)}};
}

ArmedRangeFound find(const Song& song, const ArmedRange& range)
{
    if (const auto* of = std::get_if<ArmedRange::OfSection>(&range.source))
    {
        const auto index = sectionIndex(song, of->section);
        if (!index)
            return gone("its section is gone");
        return {ArmedSpan{song.sectionStart(*index), song.sectionStart(*index + 1)}, {}};
    }
    if (const auto* of = std::get_if<ArmedRange::OfLabels>(&range.source))
    {
        const auto from = labelTick(song, of->from);
        if (!from)
            return gone("the label " + of->from.name + " is gone");
        const auto to = labelTick(song, of->to);
        if (!to)
            return gone("the label " + of->to.name + " is gone");
        if (*from >= *to)
            return gone(of->to.name + " is no longer after " + of->from.name);
        return {ArmedSpan{*from, *to}, {}};
    }
    const auto& of = std::get<ArmedRange::OfTime>(range.source);
    if (of.from >= song.end())
        return gone("the song now ends before it");
    return {ArmedSpan{of.from, std::min(of.to, song.end())}, {}};
}

} // namespace modal::core
