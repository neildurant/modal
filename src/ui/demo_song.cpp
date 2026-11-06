// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/event.hpp>
#include <modal/core/meter_map.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/tempo_map.hpp>
#include <modal/ui/demo_song.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr std::size_t kSections = 7;
constexpr std::size_t kTracks = 10; // with notes, before the empty ones
constexpr int kEmptyTracks = 4;

struct Section
{
    const char* name; // none for the extra bar, which ends the first chorus
    int bars;
    core::TimeSignature meter;
};

constexpr std::array<Section, kSections> kSongSections{{
    {"Intro", 8, {4, 4}},
    {"Verse", 6, {7, 8}},
    {"Chorus", 12, {4, 4}},
    {nullptr, 1, {2, 4}},
    {"Bridge", 8, {3, 4}},
    {"Chorus 2", 12, {4, 4}},
    {"Outro", 6, {5, 4}},
}};

struct DemoTrack
{
    const char* name;
    std::optional<core::TimeSignature> meter; // its own, or else the song's sections'
    bool gappy;                               // leaves some bars empty
    std::uint8_t pitch;
    std::array<int, kSections> density; // in each section: 0 for silent, up to 4
};

constexpr std::array<DemoTrack, kTracks> kDemoTracks{{
    {"Drums", std::nullopt, false, 36, {3, 4, 4, 4, 2, 4, 3}},
    {"Bass", std::nullopt, false, 40, {0, 2, 3, 3, 2, 3, 2}},
    {"Piano", std::nullopt, true, 60, {2, 2, 2, 2, 0, 2, 1}},
    {"Strings", std::nullopt, true, 55, {1, 0, 1, 1, 1, 1, 1}},
    {"Lead", std::nullopt, true, 72, {0, 3, 3, 0, 2, 3, 0}},
    {"Pad", std::nullopt, false, 64, {1, 1, 0, 0, 1, 1, 1}},
    {"Ostinato", core::TimeSignature{3, 4}, false, 67, {0, 2, 2, 2, 3, 2, 0}},
    {"Clave", core::TimeSignature{5, 8}, false, 75, {0, 0, 3, 3, 0, 3, 3}},
    {"Percussion", std::nullopt, false, 70, {0, 0, 4, 4, 0, 4, 0}},
    {"Effects", std::nullopt, true, 84, {1, 0, 0, 1, 0, 0, 1}},
}};

// How far apart the notes are at densities 1 to 4.
constexpr std::array<core::NoteFraction, 4> kSteps{
    core::NoteFraction{1, 2}, core::NoteFraction{1, 4}, core::NoteFraction{1, 8},
    core::NoteFraction{1, 16}};

// Whether a gappy track leaves a bar empty: about one in five, the same each time.
bool skipped(std::size_t track, std::int64_t bar)
{
    const auto hash =
        static_cast<std::uint64_t>(track) * 7919u + static_cast<std::uint64_t>(bar) * 104729u;
    return (hash >> 3) % 5 == 0;
}

// At least one of everything a song can hold besides notes, to try each feature on: in the track
// that would have it, near the section it suits. The SysEx is under the ID for non-commercial
// use, so no instrument acts on it, and nothing resets one.
void addExamples(core::Song& song, const std::array<core::Tick, kSections + 1>& starts)
{
    const core::Tick q = song.ppqn().quarter();
    const auto verse = starts[1];
    const auto chorus = starts[2];
    const auto bridge = starts[4];
    const auto tracks = song.tracks();
    const auto add = [&](std::size_t track, const std::vector<core::Event>& added)
    {
        song.updateEvents(tracks[track]->id(),
                          [&](core::EventList& events)
                          {
                              for (const auto& event : added)
                              {
                                  events.insert(event);
                              }
                          });
    };

    // Sounds changing with the sections, with a bank select and without.
    add(1, {{chorus, core::ProgramChange{34}}});
    add(2, {{verse, core::ControlChange{0, 1}},
            {verse, core::ControlChange{32, 2}},
            {verse, core::ProgramChange{5}},
            {chorus, core::ProgramChange{48}},
            {bridge, core::ProgramChange{5}}});

    // A sustain pedal, and a fade in on the volume controller.
    add(2, {{0, core::ControlChange{64, 127}}, {4 * q - 1, core::ControlChange{64, 0}}});
    std::vector<core::Event> fade;
    for (int i = 0; i <= 8; ++i)
    {
        fade.push_back({i * q, core::ControlChange{7, static_cast<std::uint8_t>(40 + i * 10)}});
    }
    add(3, fade);

    // A lead line's expression: modulation, a bend and back, and both kinds of pressure.
    std::vector<core::Event> modulation;
    for (int i = 0; i <= 8; ++i)
    {
        modulation.push_back(
            {verse + i * q / 2, core::ControlChange{1, static_cast<std::uint8_t>(i * 12)}});
    }
    add(4, modulation);
    add(4, {{chorus, core::PitchBend{core::PitchBend::kMax / 2}},
            {chorus + q / 2, core::PitchBend{0}},
            {chorus + q, core::ChannelPressure{70}},
            {chorus + 2 * q, core::ChannelPressure{0}},
            {chorus + 2 * q, core::PolyPressure{72, 60}}});

    add(9, {{verse, core::makeSysEx({0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7})},
            {verse, core::makeMetaText(core::MetaText::Kind::Text, "A text event")},
            {verse, core::makeMetaText(core::MetaText::Kind::InstrumentName, "Effects unit")},
            {chorus, core::makeMetaText(core::MetaText::Kind::Lyric, "la la la")},
            {chorus, core::makeMetaText(core::MetaText::Kind::CuePoint, "Effects in")},
            {0, core::makeMetaText(core::MetaText::Kind::Copyright, "Made up for testing")}});

    // The conductor: a slower bridge, back up after it, with a bookmark and a mark to jump to.
    song.updateTempo([&](core::TempoMap& tempo) { tempo.set(bridge, core::Tempo::fromBpm(96)); });
    song.updateTempo([&](core::TempoMap& tempo)
                     { tempo.set(starts[5], core::Tempo::fromBpm(120)); });
    song.setLabel(chorus, "Sounds change here");
    song.setMark({'a', bridge, std::nullopt, core::Mark::kNoPitch});
}

} // namespace

core::Song demoSong(core::Ppqn ppqn)
{
    core::Song song{ppqn};
    song.title = "Test arrangement";

    // The sections' meters, and where each starts.
    core::MeterMap sections{ppqn};
    std::array<core::Tick, kSections + 1> starts{};
    core::Tick tick = 0;
    for (std::size_t i = 0; i < kSections; ++i)
    {
        const auto& section = kSongSections[i];
        sections.set(tick, section.meter);
        starts[i] = tick;
        tick += section.bars * section.meter.barLength().toTicks(ppqn);
    }
    const core::Tick end = tick;
    starts[kSections] = end;

    song.updateMeter([&](core::MeterMap& conductor) { conductor = sections; });
    song.renameSection(0, kSongSections[0].name);
    for (std::size_t i = 1; i < kSections; ++i)
    {
        if (kSongSections[i].name != nullptr)
        {
            song.splitSection(starts[i], kSongSections[i].name);
        }
    }

    const auto sectionAt = [&](core::Tick at)
    {
        std::size_t i = 0;
        while (i + 1 < kSections && starts[i + 1] <= at)
        {
            ++i;
        }
        return i;
    };

    for (std::size_t t = 0; t < kTracks; ++t)
    {
        const auto& demo = kDemoTracks[t];
        const auto id = song.addTrack(demo.name);
        song.updateTrack(id, [&](core::Track& track)
                         { track.channel = demo.pitch < 40 ? 10 : static_cast<int>(t) + 1; });
        if (demo.meter)
        {
            song.updateMeter(id, [&](core::MeterMap& meter)
                             { meter = core::MeterMap{ppqn, *demo.meter}; });
        }
        const core::MeterMap meter = song.meter(id);
        song.updateEvents(id,
                          [&](core::EventList& events)
                          {
                              for (std::int64_t bar = 1;; ++bar)
                              {
                                  const core::Tick barStart = meter.barStart(bar);
                                  if (barStart >= end)
                                  {
                                      break;
                                  }
                                  const int density = demo.density[sectionAt(barStart)];
                                  if (density == 0 || (demo.gappy && skipped(t, bar)))
                                  {
                                      continue;
                                  }
                                  const core::Tick barEnd = std::min(meter.barStart(bar + 1), end);
                                  const core::Tick step =
                                      kSteps[static_cast<std::size_t>(density - 1)].toTicks(ppqn);
                                  for (core::Tick at = barStart; at < barEnd; at += step)
                                  {
                                      const core::Tick length = std::min(step, barEnd - at);
                                      events.insert(
                                          {at, core::Note{demo.pitch, 100, length * 3 / 4}});
                                  }
                              }
                          });
    }
    for (int i = 0; i < kEmptyTracks; ++i)
    {
        song.addTrack();
    }
    song.fitToContent();
    addExamples(song, starts);
    return song;
}

} // namespace modal::ui
