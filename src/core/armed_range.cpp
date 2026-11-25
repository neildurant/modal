// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/armed_range.hpp>
#include <modal/core/song.hpp>

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

std::optional<SavedRange::LabelAt> savedLabel(const Song& song, const ArmedRange::LabelAt& at)
{
    const auto index = sectionIndex(song, at.section);
    if (!index)
        return std::nullopt;
    return SavedRange::LabelAt{*index, at.name};
}

std::optional<ArmedRange::LabelAt> restoredLabel(const Song& song, const SavedRange::LabelAt& at)
{
    if (at.section >= song.sections().size())
        return std::nullopt;
    return ArmedRange::LabelAt{song.sections()[at.section]->id(), at.name};
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

std::optional<SavedRange> saved(const Song& song, const ArmedRange& range)
{
    if (const auto* of = std::get_if<ArmedRange::OfSection>(&range.source))
    {
        const auto index = sectionIndex(song, of->section);
        if (!index)
            return std::nullopt;
        return SavedRange{SavedRange::OfSection{*index}};
    }
    if (const auto* of = std::get_if<ArmedRange::OfLabels>(&range.source))
    {
        auto from = savedLabel(song, of->from);
        auto to = savedLabel(song, of->to);
        if (!from || !to)
            return std::nullopt;
        return SavedRange{SavedRange::OfLabels{std::move(*from), std::move(*to)}};
    }
    return SavedRange{std::get<ArmedRange::OfTime>(range.source)};
}

std::optional<ArmedRange> restored(const Song& song, const SavedRange& range)
{
    if (const auto* of = std::get_if<SavedRange::OfSection>(&range.source))
    {
        if (of->section >= song.sections().size())
            return std::nullopt;
        return ArmedRange::section(song, of->section);
    }
    if (const auto* of = std::get_if<SavedRange::OfLabels>(&range.source))
    {
        auto from = restoredLabel(song, of->from);
        auto to = restoredLabel(song, of->to);
        if (!from || !to)
            return std::nullopt;
        return ArmedRange{ArmedRange::OfLabels{std::move(*from), std::move(*to)}};
    }
    return ArmedRange{std::get<ArmedRange::OfTime>(range.source)};
}

} // namespace modal::core
