#include <modal/core/song.hpp>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
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
    sections_.push_back(
        std::make_shared<Section>("Song", meter.barStart(kNewSongBars + 1), meter, kDefaultTempo));
    rebuildConductor();
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
