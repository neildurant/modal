// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Helpers for tests that play songs: building them, and recording what is sent.

#include "edit_track.hpp"

#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>
#include <modal/core/song.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::test
{

using core::DestinationId;
using core::MessageSink;
using core::Ppqn;
using core::Song;
using core::Tempo;
using core::Track;
using core::TrackId;

struct Sent
{
    DestinationId destination;
    std::vector<std::uint8_t> bytes;
    std::chrono::nanoseconds due;

    friend bool operator==(const Sent&, const Sent&) = default;
};

class RecordingSink : public MessageSink
{
public:
    void send(DestinationId destination, std::span<const std::uint8_t> bytes,
              std::chrono::nanoseconds due) noexcept override
    {
        sent.push_back({destination, {bytes.begin(), bytes.end()}, due});
    }

    // The messages sent, without destinations or times.
    std::vector<std::vector<std::uint8_t>> messages() const
    {
        std::vector<std::vector<std::uint8_t>> result;
        for (const auto& s : sent)
        {
            result.push_back(s.bytes);
        }
        return result;
    }

    std::vector<Sent> sent;
};

// Routes each track to the destination numbered by its port name, or to 1 if it has none.
inline std::optional<DestinationId> byPortName(const Track& track)
{
    if (track.port.empty())
    {
        return DestinationId{1};
    }
    return DestinationId{static_cast<std::uint32_t>(std::stoul(track.port))};
}

// A song at 120 BPM, so a quarter note is 500 ms at any resolution.
inline Song songAt(Ppqn ppqn)
{
    Song song{ppqn};
    song.updateTempo([&](core::TempoMap& tempo) { tempo.set(0, Tempo{500'000}); });
    return song;
}

inline TrackId addTrack(Song& song, int channel, const std::function<void(EditTrack&)>& edit = {})
{
    const auto id = song.addTrack();
    editTrack(song, id,
              [&](EditTrack& track)
              {
                  track.channel = channel;
                  if (edit)
                  {
                      edit(track);
                  }
              });
    return id;
}

} // namespace modal::test
