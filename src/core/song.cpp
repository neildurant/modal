#include <modal/core/song.hpp>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace modal::core
{

Song::Song(Ppqn ppqn)
    : tempo_(ppqn)
    , defaultMeter_(ppqn)
{
}

void Song::setMarker(Tick tick, std::string name)
{
    assert(tick >= 0);
    const auto it = std::ranges::lower_bound(markers_, tick, {}, &Marker::tick);
    if (it != markers_.end() && it->tick == tick)
    {
        it->name = std::move(name);
    }
    else
    {
        markers_.insert(it, {tick, std::move(name)});
    }
}

bool Song::removeMarker(Tick tick)
{
    const auto it = std::ranges::find(markers_, tick, &Marker::tick);
    if (it == markers_.end())
    {
        return false;
    }
    markers_.erase(it);
    return true;
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
    auto track = std::make_shared<Track>(id, ppqn());
    track->name = std::move(name);
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(position), std::move(track));
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
    assert(copy->meter.ppqn() == ppqn());
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
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::optional<TrackId> Song::referenceTrack() const
{
    if (referenceTrack_ && track(*referenceTrack_))
    {
        return referenceTrack_;
    }
    if (tracks_.empty())
    {
        return std::nullopt;
    }
    return tracks_.front()->id();
}

void Song::setReferenceTrack(std::optional<TrackId> id)
{
    referenceTrack_ = id;
}

const MeterMap& Song::referenceMeter() const
{
    const auto id = referenceTrack();
    return id ? track(*id)->meter : defaultMeter_;
}

std::size_t Song::indexOf(TrackId id) const
{
    const auto it = std::ranges::find_if(tracks_, [id](const auto& t) { return t->id() == id; });
    return static_cast<std::size_t>(std::distance(tracks_.begin(), it));
}

} // namespace modal::core
