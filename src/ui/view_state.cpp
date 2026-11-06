// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/view_state.hpp>

#include <algorithm>

namespace modal::ui
{

namespace
{

// The tags, as ViewKind is numbered, plus one, 0 being none.
constexpr std::uint8_t kArrange = 1;
constexpr std::uint8_t kPianoRoll = 2;
constexpr std::uint8_t kEventList = 3;
constexpr std::uint8_t kTracks = 4;

std::int64_t number(std::size_t value)
{
    return static_cast<std::int64_t>(value);
}

std::size_t count(std::int64_t value)
{
    return static_cast<std::size_t>(std::max<std::int64_t>(value, 0));
}

} // namespace

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const ArrangeState& state)
{
    return {track,
            tick,
            {number(state.topTrack), state.leftTick, number(state.zoom),
             state.strip ? number(*state.strip) : -1},
            kArrange};
}

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const PianoRollState& state)
{
    return {track,
            tick,
            {state.pitch, state.topPitch, number(state.pageSize), state.viewStart,
             state.viewLength.value_or(0)},
            kPianoRoll};
}

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const EventListState& state)
{
    return {track, tick, {number(state.top), number(state.down)}, kEventList};
}

core::EditPlace placeWith(std::optional<core::TrackId> track, core::Tick tick,
                          const TracksState& state)
{
    return {track,
            tick,
            {number(state.top), number(state.column), state.left, state.pitch ? *state.pitch : -1},
            kTracks};
}

std::optional<TracksState> tracksStateOf(const core::EditPlace& place)
{
    if (place.viewKind != kTracks || place.view.size() != 4)
        return std::nullopt;
    const auto& v = place.view;
    return TracksState{
        count(v[0]), count(v[1]), static_cast<int>(std::clamp<std::int64_t>(v[2], 0, 1000000)),
        v[3] >= 0 && v[3] <= 127 ? std::optional{static_cast<std::uint8_t>(v[3])} : std::nullopt};
}

std::optional<ArrangeState> arrangeStateOf(const core::EditPlace& place)
{
    const auto& v = place.view;
    if (place.viewKind != kArrange || v.size() != 4)
    {
        return std::nullopt;
    }
    return ArrangeState{.topTrack = count(v[0]),
                        .leftTick = v[1],
                        .zoom = count(v[2]),
                        .strip = v[3] >= 0 ? std::optional{count(v[3])} : std::nullopt};
}

std::optional<PianoRollState> pianoRollStateOf(const core::EditPlace& place)
{
    const auto& v = place.view;
    if (place.viewKind != kPianoRoll || v.size() != 5)
    {
        return std::nullopt;
    }
    return PianoRollState{
        .pitch = static_cast<std::uint8_t>(std::clamp<std::int64_t>(v[0], 0, 127)),
        .topPitch = static_cast<std::uint8_t>(std::clamp<std::int64_t>(v[1], 0, 127)),
        .pageSize = count(v[2]),
        .viewStart = std::max<core::Tick>(v[3], 0),
        .viewLength = v[4] > 0 ? std::optional{v[4]} : std::nullopt};
}

std::optional<EventListState> eventListStateOf(const core::EditPlace& place)
{
    const auto& v = place.view;
    if (place.viewKind != kEventList || v.size() != 2)
    {
        return std::nullopt;
    }
    return EventListState{.top = count(v[0]), .down = count(v[1])};
}

} // namespace modal::ui
