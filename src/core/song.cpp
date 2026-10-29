// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/song.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>

namespace modal::core
{

namespace
{

constexpr Tick kForever = std::numeric_limits<Tick>::max();

bool onBarLine(const MeterMap& meter, Tick tick)
{
    const auto position = meter.toBarBeatTick(tick);
    return position.beat == 1 && position.tick == 0;
}

Tick barLineAtOrAfter(const MeterMap& meter, Tick tick)
{
    return onBarLine(meter, tick) ? tick : meter.barStart(meter.toBarBeatTick(tick).bar + 1);
}

// The part of a meter from one tick until another, positioned from the first, where a new
// bar starts.
MeterMap meterBetween(const MeterMap& meter, Tick from, Tick to)
{
    MeterMap part{meter.ppqn(), meter.signatureAt(from)};
    for (const auto& change : meter.changes())
    {
        if (change.tick > from && change.tick < to)
        {
            part.set(change.tick - from, change.signature);
        }
    }
    return part;
}

// Makes a bar that would run past the end a remainder bar ending there. Returns whether there
// was one to cut.
bool cutAt(MeterMap& meter, Tick end)
{
    if (onBarLine(meter, end))
    {
        return false;
    }
    // Any other meter from the end makes the bar before it a remainder bar. Nothing after the
    // end matters, so the change goes again.
    const TimeSignature other =
        meter.signatureAt(end) == TimeSignature{1, 4} ? TimeSignature{1, 8} : TimeSignature{1, 4};
    meter.remove(meter.set(end, other));
    return true;
}

// The events starting from one tick until another, positioned from the first.
EventList eventsBetween(const EventList& events, Tick from, Tick to)
{
    EventList part;
    for (auto event : events.startingIn(from, to))
    {
        event.tick -= from;
        part.insert(std::move(event));
    }
    return part;
}

// Where an event ends: a note's end, or just after any other event.
Tick endOf(const Event& event)
{
    const auto* note = std::get_if<Note>(&event.data);
    return event.tick + (note ? std::max(note->duration, Tick{1}) : Tick{1});
}

Tempo tempoAt(std::span<const TempoChange> changes, Tick tick)
{
    const auto it = std::ranges::upper_bound(changes, tick, {}, &TempoChange::tick);
    return std::prev(it)->tempo;
}

} // namespace

Section::Section(std::string name, Tick length, MeterMap meter, Tempo tempo)
    : name_(std::move(name))
    , length_(length)
    , tempo_{{0, tempo}}
    , meter_(std::move(meter))
{
    assert(length > 0);
}

const TrackContent* Section::content(TrackId id) const
{
    const auto it = content_.find(id);
    return it == content_.end() ? nullptr : it->second.get();
}

void Section::addTempo(TempoChange change)
{
    assert(change.tick >= 0 && change.tick < length_);
    if (change.tick == 0)
    {
        tempo_.front() = change;
        return;
    }
    assert(change.tick > tempo_.back().tick);
    tempo_.push_back(change);
}

void Section::addMarker(Marker marker)
{
    assert(marker.tick >= 0 && marker.tick < length_);
    assert(markers_.empty() || marker.tick > markers_.back().tick);
    markers_.push_back(std::move(marker));
}

void Section::setContent(TrackId id, TrackContent content)
{
    if (!content.meter && content.events.empty())
    {
        content_.erase(id);
        return;
    }
    content_[id] = std::make_shared<const TrackContent>(std::move(content));
}

bool operator==(const Section& a, const Section& b)
{
    const auto sameContent = [](const auto& x, const auto& y)
    { return x.first == y.first && (x.second == y.second || *x.second == *y.second); };
    return a.name_ == b.name_ && a.colour_ == b.colour_ && a.length_ == b.length_ &&
           a.tempo_ == b.tempo_ && a.meter_ == b.meter_ && a.markers_ == b.markers_ &&
           std::ranges::equal(a.content_, b.content_, sameContent);
}

Song::Song(Ppqn ppqn)
    : tempo_(ppqn)
    , meter_(ppqn)
{
    const MeterMap meter{ppqn};
    sections_.push_back(std::make_shared<Section>("Section 1", meter.barStart(kNewSongBars + 1),
                                                  meter, kDefaultTempo));
    rebuildConductor();
}

Song::Song(Ppqn ppqn, std::vector<Track> tracks, std::vector<Section> sections)
    : tempo_(ppqn)
    , meter_(ppqn)
{
    assert(!sections.empty());
    for (auto& track : tracks)
    {
        nextTrackId_ = std::max(nextTrackId_, track.id().value + 1);
        tracks_.push_back(std::make_shared<const Track>(std::move(track)));
    }
    for (auto& section : sections)
    {
        assert(section.meter_.ppqn() == ppqn);
        sections_.push_back(std::make_shared<const Section>(std::move(section)));
    }
    rebuildAll();
}

Song::Song(Parts parts)
    : title(std::move(parts.title))
    , metronome(std::move(parts.metronome))
    , countIn(parts.countIn)
    , tracks_(std::move(parts.tracks))
    , sections_(std::move(parts.sections))
    , nextTrackId_(parts.nextTrackId)
    , tempo_(parts.ppqn)
    , meter_(parts.ppqn)
{
    assert(!sections_.empty());
    rebuildAll();
}

Song::Parts Song::parts() const
{
    return {ppqn(), title, metronome, countIn, tracks_, sections_, nextTrackId_};
}

bool operator==(const Song& a, const Song& b)
{
    const auto same = [](const auto& x, const auto& y) { return x == y || *x == *y; };
    return a.ppqn() == b.ppqn() && a.title == b.title && a.metronome == b.metronome &&
           a.countIn == b.countIn && std::ranges::equal(a.tracks_, b.tracks_, same) &&
           std::ranges::equal(a.sections_, b.sections_, same);
}

const Track* Song::track(TrackId id) const
{
    const auto index = indexOf(id);
    return index < tracks_.size() ? tracks_[index].get() : nullptr;
}

TrackId Song::addTrack(std::string name)
{
    return addTrack(tracks_.size(), std::move(name));
}

TrackId Song::addTrack(std::size_t position, std::string name)
{
    assert(position <= tracks_.size());
    const TrackId id{nextTrackId_++};
    auto track = std::make_shared<Track>(id);
    track->name = std::move(name);
    const auto offset = static_cast<std::ptrdiff_t>(position);
    tracks_.insert(tracks_.begin() + offset, std::move(track));
    flattened_.insert(flattened_.begin() + offset,
                      Flattened{meter_, std::make_shared<const EventList>()});
    return id;
}

bool Song::updateTrack(TrackId id, const std::function<void(Track&)>& edit)
{
    const auto index = indexOf(id);
    if (index == tracks_.size())
    {
        return false;
    }
    auto copy = std::make_shared<Track>(*tracks_[index]);
    edit(*copy);
    assert(copy->id() == id);
    tracks_[index] = std::move(copy);
    return true;
}

bool Song::removeTrack(TrackId id)
{
    const auto index = indexOf(id);
    if (index == tracks_.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        if (sections_[i]->content(id))
        {
            auto section = std::make_shared<Section>(*sections_[i]);
            section->content_.erase(id);
            sections_[i] = std::move(section);
        }
    }
    const auto offset = static_cast<std::ptrdiff_t>(index);
    tracks_.erase(tracks_.begin() + offset);
    flattened_.erase(flattened_.begin() + offset);
    return true;
}

std::size_t Song::sectionAt(Tick tick) const
{
    const auto it = std::ranges::upper_bound(starts_.begin(), std::prev(starts_.end()), tick);
    return static_cast<std::size_t>(std::max<std::ptrdiff_t>(it - starts_.begin() - 1, 0));
}

std::optional<Song::Split> Song::splitSection(Tick tick, std::string name)
{
    if (tick <= 0 || tick >= end())
    {
        return std::nullopt;
    }
    const auto index = sectionAt(tick);
    const Tick at = tick - starts_[index];
    if (at == 0)
    {
        return std::nullopt;
    }
    const Section& old = *sections_[index];

    auto first = std::make_shared<Section>(old);
    auto second =
        std::make_shared<Section>(uniqueSectionName(std::move(name)), old.length_ - at,
                                  meterBetween(old.meter_, at, kForever), tempoAt(old.tempo_, at));
    first->length_ = at;
    first->meter_ = meterBetween(old.meter_, 0, at);
    Split split{index + 1, cutAt(first->meter_, at), {}};

    // The new section starts a new bar, so the rest of the old one may no longer be whole
    // bars: the last section grows to the next bar line, and any other ends in a remainder.
    if (index + 1 == sections_.size())
    {
        second->length_ = barLineAtOrAfter(second->meter_, second->length_);
    }
    else
    {
        cutAt(second->meter_, second->length_);
    }

    std::erase_if(first->tempo_, [at](const auto& change) { return change.tick >= at; });
    for (const auto& change : old.tempo_)
    {
        if (change.tick > at)
        {
            second->tempo_.push_back({change.tick - at, change.tempo});
        }
    }

    std::erase_if(first->markers_, [at](const auto& marker) { return marker.tick >= at; });
    for (const auto& marker : old.markers_)
    {
        if (marker.tick >= at)
        {
            second->markers_.push_back({marker.tick - at, marker.name});
        }
    }

    first->content_.clear();
    for (const auto& [id, content] : old.content_)
    {
        TrackContent before{std::nullopt, eventsBetween(content->events, 0, at)};
        TrackContent after{std::nullopt, eventsBetween(content->events, at, kForever)};
        if (content->meter)
        {
            before.meter = meterBetween(*content->meter, 0, at);
            if (cutAt(*before.meter, at))
            {
                split.tracks.push_back(id);
            }
            after.meter = meterBetween(*content->meter, at, kForever);
        }
        if (before.meter || !before.events.empty())
        {
            first->content_.emplace(id, std::make_shared<const TrackContent>(std::move(before)));
        }
        if (after.meter || !after.events.empty())
        {
            second->content_.emplace(id, std::make_shared<const TrackContent>(std::move(after)));
        }
    }
    std::ranges::sort(split.tracks, {}, [this](TrackId id) { return indexOf(id); });

    sections_[index] = std::move(first);
    sections_.insert(sections_.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(second));
    rebuildConductor();
    rebuildMeters();
    return split;
}

std::string Song::newSectionName() const
{
    std::int64_t highest = 0;
    for (const auto& section : sections_)
    {
        const std::string_view name = section->name_;
        constexpr std::string_view kPrefix = "Section ";
        std::int64_t number = 0;
        const auto* end = name.data() + name.size();
        if (name.starts_with(kPrefix) &&
            std::from_chars(name.data() + kPrefix.size(), end, number).ptr == end)
        {
            highest = std::max(highest, number);
        }
    }
    return "Section " + std::to_string(highest + 1);
}

bool Song::removeSections(std::size_t index, std::size_t count)
{
    count = std::min(count, sections_.size() - std::min(index, sections_.size()));
    if (count == 0 || count >= sections_.size())
    {
        return false;
    }
    const auto first = sections_.begin() + static_cast<std::ptrdiff_t>(index);
    sections_.erase(first, first + static_cast<std::ptrdiff_t>(count));
    rebuildAll();
    return true;
}

bool Song::insertSections(std::size_t index,
                          std::span<const std::shared_ptr<const Section>> sections)
{
    index = std::min(index, sections_.size());
    for (const auto& section : sections)
    {
        auto copy = std::make_shared<Section>(*section);
        copy->name_ = uniqueSectionName(copy->name_);
        std::erase_if(copy->content_,
                      [this](const auto& entry)
                      {
                          return std::ranges::none_of(tracks_, [&](const auto& track)
                                                      { return track->id() == entry.first; });
                      });
        sections_.insert(sections_.begin() + static_cast<std::ptrdiff_t>(index++), std::move(copy));
    }
    if (sections.empty())
    {
        return false;
    }
    rebuildAll();
    return true;
}

bool Song::insertEmptySection(std::size_t index, std::string name, std::int64_t bars)
{
    if (bars < 1)
    {
        return false;
    }
    index = std::min(index, sections_.size());
    // The meter and tempo where it goes: at the end of the section before, or the start of the
    // first.
    TimeSignature signature = sections_.front()->meter_.signatureAt(0);
    Tempo tempo = sections_.front()->tempo_.front().tempo;
    if (index > 0)
    {
        const Section& before = *sections_[index - 1];
        signature = before.meter_.signatureAt(before.length_ - 1);
        tempo = tempoAt(before.tempo_, before.length_ - 1);
    }
    MeterMap meter{ppqn(), signature};
    sections_.insert(sections_.begin() + static_cast<std::ptrdiff_t>(index),
                     std::make_shared<Section>(uniqueSectionName(std::move(name)),
                                               meter.barStart(bars + 1), meter, tempo));
    rebuildAll();
    return true;
}

std::size_t Song::moveSection(std::size_t index, std::ptrdiff_t by)
{
    if (index >= sections_.size())
    {
        return index;
    }
    const auto to = static_cast<std::size_t>(
        std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(index) + by, 0,
                                   static_cast<std::ptrdiff_t>(sections_.size()) - 1));
    if (to == index)
    {
        return index;
    }
    auto section = std::move(sections_[index]);
    sections_.erase(sections_.begin() + static_cast<std::ptrdiff_t>(index));
    sections_.insert(sections_.begin() + static_cast<std::ptrdiff_t>(to), std::move(section));
    rebuildAll();
    return to;
}

bool Song::resizeSection(std::size_t index, std::int64_t bars)
{
    if (index >= sections_.size() || bars == 0)
    {
        return false;
    }
    const Section& old = *sections_[index];
    auto section = std::make_shared<Section>(old);
    if (bars > 0)
    {
        // More bars of the last bar's meter, a whole bar of it even after a remainder bar.
        const auto signature = old.meter_.signatureAt(old.length_ - 1);
        section->meter_.set(old.length_, signature);
        MeterMap added{ppqn(), signature};
        section->length_ = old.length_ + added.barStart(bars + 1);
        for (auto& [id, content] : section->content_)
        {
            if (content->meter)
            {
                auto longer = *content;
                longer.meter->set(old.length_, longer.meter->signatureAt(old.length_ - 1));
                content = std::make_shared<const TrackContent>(std::move(longer));
            }
        }
    }
    else
    {
        const auto last = old.meter_.toBarBeatTick(old.length_ - 1).bar;
        if (last + bars < 1)
        {
            return false;
        }
        const Tick end = old.meter_.barStart(last + bars + 1);
        section->length_ = end;
        section->meter_ = meterBetween(old.meter_, 0, end);
        std::erase_if(section->tempo_, [end](const auto& change) { return change.tick >= end; });
        std::erase_if(section->markers_, [end](const auto& marker) { return marker.tick >= end; });
        for (auto it = section->content_.begin(); it != section->content_.end();)
        {
            TrackContent shorter{std::nullopt, eventsBetween(it->second->events, 0, end)};
            if (it->second->meter)
            {
                shorter.meter = meterBetween(*it->second->meter, 0, end);
                cutAt(*shorter.meter, end);
            }
            if (!shorter.meter && shorter.events.empty())
            {
                it = section->content_.erase(it);
                continue;
            }
            it->second = std::make_shared<const TrackContent>(std::move(shorter));
            ++it;
        }
    }
    sections_[index] = std::move(section);
    rebuildAll();
    return true;
}

bool Song::joinSections(std::size_t index)
{
    if (index + 1 >= sections_.size())
    {
        return false;
    }
    const Section& first = *sections_[index];
    const Section& second = *sections_[index + 1];
    const Tick at = first.length_;
    auto joined = std::make_shared<Section>(first);
    joined->length_ = first.length_ + second.length_;

    // A meter, then another from where the first section ends.
    const auto followedBy = [at](MeterMap meter, const MeterMap& after)
    {
        for (const auto& change : after.changes())
        {
            meter.set(at + change.tick, change.signature);
        }
        return meter;
    };
    joined->meter_ = followedBy(first.meter_, second.meter_);
    for (const auto& change : second.tempo_)
    {
        if (change.tick > 0 || change.tempo != tempoAt(first.tempo_, at - 1))
        {
            joined->tempo_.push_back({at + change.tick, change.tempo});
        }
    }
    for (const auto& marker : second.markers_)
    {
        // Names stay unique within the section.
        std::string name = marker.name;
        for (int n = 2;
             std::ranges::find(joined->markers_, name, &Marker::name) != joined->markers_.end();
             ++n)
        {
            name = marker.name + " " + std::to_string(n);
        }
        joined->markers_.push_back({at + marker.tick, std::move(name)});
    }

    joined->content_.clear();
    std::vector<TrackId> ids;
    for (const auto& [id, content] : first.content_)
    {
        ids.push_back(id);
    }
    for (const auto& [id, content] : second.content_)
    {
        if (!first.content_.contains(id))
        {
            ids.push_back(id);
        }
    }
    for (const auto id : ids)
    {
        const auto* before = first.content(id);
        const auto* after = second.content(id);
        TrackContent content{std::nullopt, before ? before->events : EventList{}};
        if (after)
        {
            for (auto event : after->events.all())
            {
                event.tick += at;
                content.events.insert(std::move(event));
            }
        }
        if ((before && before->meter) || (after && after->meter))
        {
            content.meter = followedBy(before && before->meter ? *before->meter : first.meter_,
                                       after && after->meter ? *after->meter : second.meter_);
        }
        joined->content_.emplace(id, std::make_shared<const TrackContent>(std::move(content)));
    }

    sections_[index] = std::move(joined);
    sections_.erase(sections_.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    rebuildAll();
    return true;
}

Song::BarsEdit Song::removeBars(Tick from, std::int64_t count)
{
    BarsEdit result;
    if (count < 1 || from < 0 || from >= end() || !onBarLine(meter_, from))
    {
        return result;
    }
    const Tick to = std::min(meter_.barStart(meter_.toBarBeatTick(from).bar + count), end());
    if (from == 0 && to == end())
    {
        return result; // a song keeps a bar
    }
    // The bars cut out as sections of their own, then removed, and the section they were cut
    // from joined up again.
    const auto first = sectionAt(from);
    const auto last = sectionAt(to - 1);
    Song edited = *this;
    for (const Tick tick : {from, to})
    {
        if (tick < edited.end() && edited.starts_[edited.sectionAt(tick)] != tick)
        {
            const auto split = edited.splitSection(tick, sections_[sectionAt(tick)]->name_);
            if (!split || !split->tracks.empty())
            {
                result.misaligned = split ? std::optional{split->tracks.front()} : std::nullopt;
                return result;
            }
        }
    }
    const auto firstCut = edited.sectionAt(from);
    const auto cuts = edited.sectionAt(to - 1) - firstCut + 1;
    for (std::size_t i = 0; i < cuts; ++i)
    {
        auto piece = std::make_shared<Section>(*edited.sections_[firstCut + i]);
        piece->name_ = sections_[sectionAt(edited.starts_[firstCut + i])]->name_;
        result.removed.push_back(std::move(piece));
    }
    edited.removeSections(firstCut, cuts);
    const bool before = starts_[first] < from;
    const bool after = to < starts_[last] + sections_[last]->length_;
    if (before && after && first == last)
    {
        edited.joinSections(firstCut - 1);
    }
    else if (after)
    {
        // What's left of the last section, under its own name again.
        auto rest = std::make_shared<Section>(*edited.sections_[firstCut]);
        rest->name_ = sections_[last]->name_;
        rest->colour_ = sections_[last]->colour_;
        edited.replaceSection(firstCut, std::move(rest));
    }
    *this = std::move(edited);
    result.done = true;
    return result;
}

Song::BarsEdit Song::insertBars(Tick at, std::int64_t count, std::optional<TimeSignature> signature)
{
    BarsEdit result;
    if (count < 1 || at < 0 || at >= end() || !onBarLine(meter_, at))
    {
        return result;
    }
    const auto index = sectionAt(at);
    const Section& old = *sections_[index];
    const Tick offset = at - starts_[index];
    const MeterMap meter{ppqn(), signature.value_or(old.meter_.signatureAt(offset))};
    auto added = std::make_shared<Section>(old.name_, meter.barStart(count + 1), meter,
                                           tempoAt(old.tempo_, offset));
    for (const auto& [id, content] : old.content_)
    {
        if (content->meter)
        {
            const MeterMap own{ppqn(), content->meter->signatureAt(offset)};
            if (!onBarLine(own, added->length_))
            {
                result.misaligned = id;
                return result;
            }
            added->content_.emplace(
                id, std::make_shared<const TrackContent>(TrackContent{own, EventList{}}));
        }
    }
    Song edited = *this;
    if (offset > 0)
    {
        const auto split = edited.splitSection(at, old.name_);
        if (!split || !split->tracks.empty())
        {
            result.misaligned = split ? std::optional{split->tracks.front()} : std::nullopt;
            return result;
        }
    }
    const auto place = offset > 0 ? index + 1 : index;
    edited.sections_.insert(edited.sections_.begin() + static_cast<std::ptrdiff_t>(place),
                            std::move(added));
    edited.rebuildAll();
    edited.joinSections(index);
    if (offset > 0)
    {
        edited.joinSections(index);
    }
    // Under the section's own name and colour, which the bars went into.
    auto joined = std::make_shared<Section>(*edited.sections_[index]);
    joined->name_ = old.name_;
    joined->colour_ = old.colour_;
    edited.replaceSection(index, std::move(joined));
    *this = std::move(edited);
    result.done = true;
    return result;
}

bool Song::renameSection(std::size_t index, std::string name)
{
    if (index >= sections_.size())
    {
        return false;
    }
    auto section = std::make_shared<Section>(*sections_[index]);
    section->name_ = uniqueSectionName(std::move(name), index);
    replaceSection(index, std::move(section));
    return true;
}

void Song::fitToContent()
{
    const auto last = sections_.size() - 1;
    auto section = std::make_shared<Section>(*sections_[last]);

    Tick extent = 1;
    for (const auto& [id, content] : section->content_)
    {
        for (const auto& event : content->events.all())
        {
            extent = std::max(extent, endOf(event));
        }
    }
    for (const auto& marker : section->markers_)
    {
        extent = std::max(extent, marker.tick + 1);
    }
    for (const auto& change : section->tempo_)
    {
        extent = std::max(extent, change.tick + 1);
    }

    section->length_ = barLineAtOrAfter(section->meter_, extent);
    section->meter_ = meterBetween(section->meter_, 0, section->length_);
    for (auto& [id, content] : section->content_)
    {
        if (content->meter)
        {
            auto trimmed = *content;
            trimmed.meter = meterBetween(*content->meter, 0, section->length_);
            content = std::make_shared<const TrackContent>(std::move(trimmed));
        }
    }
    replaceSection(last, std::move(section));
    rebuildConductor();
    rebuildMeters();
}

const MeterMap& Song::meter(TrackId id) const
{
    const auto index = indexOf(id);
    assert(index < tracks_.size());
    return index < tracks_.size() ? flattened_[index].meter : meter_;
}

const EventList& Song::events(TrackId id) const
{
    static const EventList kNone;
    const auto index = indexOf(id);
    assert(index < tracks_.size());
    return index < tracks_.size() ? *flattened_[index].events : kNone;
}

void Song::updateTempo(const std::function<void(TempoMap&)>& edit)
{
    TempoMap tempo = tempo_;
    edit(tempo);
    assert(tempo.ppqn() == ppqn());
    growTo(tempo.changes().back().tick + 1);
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        const Tick start = starts_[i];
        auto section = std::make_shared<Section>(*sections_[i]);
        section->tempo_ = {{0, tempo.tempoAt(start)}};
        for (const auto& change : tempo.changes())
        {
            if (change.tick > start && change.tick < starts_[i + 1])
            {
                section->tempo_.push_back({change.tick - start, change.tempo});
            }
        }
        replaceSection(i, std::move(section));
    }
    rebuildConductor();
}

void Song::updateMeter(const std::function<void(MeterMap&)>& edit)
{
    MeterMap meter = meter_;
    edit(meter);
    assert(meter.ppqn() == ppqn());
    growTo(meter.changes().back().tick + 1);
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        auto section = std::make_shared<Section>(*sections_[i]);
        section->meter_ = meterBetween(meter, starts_[i], starts_[i + 1]);
        if (i + 1 == sections_.size())
        {
            section->length_ = barLineAtOrAfter(section->meter_, section->length_);
        }
        else
        {
            cutAt(section->meter_, section->length_);
        }
        replaceSection(i, std::move(section));
    }
    rebuildConductor();
    rebuildMeters();
}

bool Song::updateMeter(TrackId id, const std::function<void(MeterMap&)>& edit)
{
    const auto index = indexOf(id);
    if (index == tracks_.size())
    {
        return false;
    }
    MeterMap meter = flattened_[index].meter;
    edit(meter);
    assert(meter.ppqn() == ppqn());
    growTo(meter.changes().back().tick + 1);
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        updateContent(i, id, [&](TrackContent& content)
                      { content.meter = meterBetween(meter, starts_[i], starts_[i + 1]); });
    }
    rebuildConductor();
    rebuildMeters();
    return true;
}

bool Song::updateEvents(TrackId id, const std::function<void(EventList&)>& edit)
{
    const auto index = indexOf(id);
    if (index == tracks_.size())
    {
        return false;
    }
    EventList events = *flattened_[index].events;
    edit(events);
    Tick extent = 0;
    for (const auto& event : events.all())
    {
        extent = std::max(extent, endOf(event));
    }
    growTo(extent);
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        updateContent(i, id, [&](TrackContent& content)
                      { content.events = eventsBetween(events, starts_[i], starts_[i + 1]); });
    }
    flattened_[index].events = std::make_shared<const EventList>(std::move(events));
    return true;
}

bool Song::followConductor(TrackId id)
{
    const auto index = indexOf(id);
    if (index == tracks_.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        updateContent(i, id, [](TrackContent& content) { content.meter.reset(); });
    }
    rebuildMeters();
    return true;
}

void Song::setMarker(Tick tick, std::string name)
{
    assert(tick >= 0);
    growTo(tick + 1);
    const auto index = sectionAt(tick);
    const Tick at = tick - starts_[index];
    auto section = std::make_shared<Section>(*sections_[index]);
    auto& markers = section->markers_;

    std::string unique = name;
    for (int n = 2; std::ranges::any_of(markers, [&](const Marker& marker)
                                        { return marker.name == unique && marker.tick != at; });
         ++n)
    {
        unique = name + " " + std::to_string(n);
    }

    const auto it = std::ranges::lower_bound(markers, at, {}, &Marker::tick);
    if (it != markers.end() && it->tick == at)
    {
        it->name = std::move(unique);
    }
    else
    {
        markers.insert(it, {at, std::move(unique)});
    }
    sections_[index] = std::move(section);
    rebuildConductor();
}

bool Song::removeMarker(Tick tick)
{
    if (tick < 0 || tick >= end())
    {
        return false;
    }
    const auto index = sectionAt(tick);
    const Tick at = tick - starts_[index];
    const auto& markers = sections_[index]->markers_;
    const auto it = std::ranges::find(markers, at, &Marker::tick);
    if (it == markers.end())
    {
        return false;
    }
    auto section = std::make_shared<Section>(*sections_[index]);
    section->markers_.erase(section->markers_.begin() + std::distance(markers.begin(), it));
    sections_[index] = std::move(section);
    rebuildConductor();
    return true;
}

std::size_t Song::indexOf(TrackId id) const
{
    const auto it = std::ranges::find_if(tracks_, [id](const auto& t) { return t->id() == id; });
    return static_cast<std::size_t>(std::distance(tracks_.begin(), it));
}

void Song::growTo(Tick end)
{
    if (end <= this->end())
    {
        return;
    }
    const auto last = sections_.size() - 1;
    auto section = std::make_shared<Section>(*sections_[last]);
    section->length_ = barLineAtOrAfter(section->meter_, end - starts_[last]);
    sections_[last] = std::move(section);
    starts_.back() = starts_[last] + sections_[last]->length_;
}

void Song::replaceSection(std::size_t index, std::shared_ptr<Section> section)
{
    if (*section != *sections_[index])
    {
        sections_[index] = std::move(section);
    }
}

void Song::updateContent(std::size_t index, TrackId id,
                         const std::function<void(TrackContent&)>& edit)
{
    const auto* old = sections_[index]->content(id);
    TrackContent content = old ? *old : TrackContent{};
    edit(content);
    const bool empty = !content.meter && content.events.empty();
    if (old ? content == *old : empty)
    {
        return;
    }
    auto section = std::make_shared<Section>(*sections_[index]);
    if (empty)
    {
        section->content_.erase(id);
    }
    else
    {
        section->content_[id] = std::make_shared<const TrackContent>(std::move(content));
    }
    sections_[index] = std::move(section);
}

void Song::rebuildConductor()
{
    const auto ppqn = tempo_.ppqn();
    tempo_ = TempoMap{ppqn, sections_.front()->tempo_.front().tempo};
    meter_ = MeterMap{ppqn, sections_.front()->meter_.changes().front().signature};
    markers_.clear();
    starts_.clear();
    Tick start = 0;
    for (const auto& section : sections_)
    {
        starts_.push_back(start);
        for (const auto& change : section->tempo_)
        {
            if (tempo_.tempoAt(start + change.tick) != change.tempo)
            {
                tempo_.set(start + change.tick, change.tempo);
            }
        }
        for (const auto& change : section->meter_.changes())
        {
            meter_.set(start + change.tick, change.signature);
        }
        for (const auto& marker : section->markers_)
        {
            markers_.push_back({start + marker.tick, marker.name});
        }
        start += section->length_;
    }
    starts_.push_back(start);
}

void Song::rebuildEvents(std::size_t trackIndex)
{
    const auto id = tracks_[trackIndex]->id();
    auto events = std::make_shared<EventList>();
    for (std::size_t s = 0; s < sections_.size(); ++s)
    {
        if (const auto* content = sections_[s]->content(id))
        {
            for (auto event : content->events.all())
            {
                event.tick += starts_[s];
                events->insert(std::move(event));
            }
        }
    }
    flattened_[trackIndex].events = std::move(events);
}

void Song::rebuildAll()
{
    rebuildConductor();
    flattened_.clear();
    for (std::size_t i = 0; i < tracks_.size(); ++i)
    {
        flattened_.push_back({meter_, nullptr});
        rebuildEvents(i);
    }
    rebuildMeters();
}

void Song::rebuildMeters()
{
    for (std::size_t i = 0; i < tracks_.size(); ++i)
    {
        const auto id = tracks_[i]->id();
        const auto partOf = [id](const Section& section) -> const MeterMap&
        {
            const auto* content = section.content(id);
            return content && content->meter ? *content->meter : section.meter_;
        };
        MeterMap meter{ppqn(), partOf(*sections_.front()).changes().front().signature};
        for (std::size_t s = 0; s < sections_.size(); ++s)
        {
            for (const auto& change : partOf(*sections_[s]).changes())
            {
                if (change.tick < sections_[s]->length_)
                {
                    meter.set(starts_[s] + change.tick, change.signature);
                }
            }
        }
        flattened_[i].meter = std::move(meter);
    }
}

std::string Song::uniqueSectionName(std::string name, std::optional<std::size_t> except) const
{
    const auto taken = [&](const std::string& candidate)
    {
        for (std::size_t i = 0; i < sections_.size(); ++i)
        {
            if (i != except && sections_[i]->name_ == candidate)
            {
                return true;
            }
        }
        return false;
    };
    std::string unique = name;
    for (int n = 2; taken(unique); ++n)
    {
        unique = name + " " + std::to_string(n);
    }
    return unique;
}

} // namespace modal::core
