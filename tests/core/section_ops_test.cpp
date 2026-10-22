// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal::core;

namespace
{

// Eight bars of 4/4 in three sections, A (bars 1-2), B (3-5) and C (6-8), and a track with a
// note at the start of each bar.
struct Sections
{
    explicit Sections(Ppqn ppqn)
        : song{ppqn}
        , bar{ppqn.whole()}
        , track{song.addTrack("Bass")}
    {
        song.updateEvents(track,
                          [&](EventList& events)
                          {
                              for (int i = 0; i < 8; ++i)
                              {
                                  events.insert({i * bar, Note{static_cast<std::uint8_t>(40 + i),
                                                               100, ppqn.quarter()}});
                              }
                          });
        song.renameSection(0, "A");
        song.splitSection(2 * bar, "B");
        song.splitSection(5 * bar, "C");
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> result;
        for (const auto& section : song.sections())
        {
            result.push_back(section->name());
        }
        return result;
    }

    // Each note's pitch, by the bar it starts in (counting from 0), as 40 plus its original bar.
    std::vector<int> pitches() const
    {
        std::vector<int> result;
        for (const auto& event : song.events(track).all())
        {
            CHECK(event.tick % bar == 0);
            result.push_back(std::get<Note>(event.data).pitch);
        }
        return result;
    }

    Song song;
    Tick bar;
    TrackId track;
};

} // namespace

TEST_CASE("a new section's name counts up from the highest Section number")
{
    Song song;
    CHECK(song.newSectionName() == "Section 2");
    song.splitSection(song.end() / 2, "Section 5");
    song.renameSection(0, "Chorus");
    CHECK(song.newSectionName() == "Section 6");
}

TEST_CASE("removing sections closes up the song after them")
{
    Sections s{MODAL_ANY_PPQN()};
    CHECK(s.song.removeSections(1, 1));
    CHECK(s.names() == std::vector<std::string>{"A", "C"});
    CHECK(s.pitches() == std::vector<int>{40, 41, 45, 46, 47});
    CHECK(s.song.end() == 5 * s.bar);

    // Never every section, however many are asked for.
    CHECK_FALSE(s.song.removeSections(0, 2));
    CHECK_FALSE(s.song.removeSections(0, 5));
    CHECK(s.song.removeSections(1, 5)); // as many as there are after it
    CHECK(s.names() == std::vector<std::string>{"A"});
}

TEST_CASE("inserted copies of sections keep what's in them, numbered if their names are taken")
{
    Sections s{MODAL_ANY_PPQN()};
    const std::vector chorus{s.song.sections()[1]};
    CHECK(s.song.insertSections(3, chorus));
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C", "B 2"});
    CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47, 42, 43, 44});
    CHECK(s.song.insertSections(0, chorus));
    CHECK(s.names().front() == "B 3");
    CHECK(s.pitches().front() == 42);
}

TEST_CASE("an empty section goes in where it's put, in the meter there")
{
    Sections s{MODAL_ANY_PPQN()};
    CHECK(s.song.insertEmptySection(1, s.song.newSectionName(), 4));
    CHECK(s.names() == std::vector<std::string>{"A", "Section 1", "B", "C"});
    CHECK(s.song.sectionStart(2) == 6 * s.bar);
    CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47});
    CHECK(s.song.events(s.track)[2].tick == 6 * s.bar);
    CHECK_FALSE(s.song.insertEmptySection(0, "None", 0));
}

TEST_CASE("a section moves past its neighbours, taking what's in it")
{
    Sections s{MODAL_ANY_PPQN()};
    CHECK(s.song.moveSection(0, 1) == 1);
    CHECK(s.names() == std::vector<std::string>{"B", "A", "C"});
    CHECK(s.pitches() == std::vector<int>{42, 43, 44, 40, 41, 45, 46, 47});
    CHECK(s.song.moveSection(1, 5) == 2); // as far as the end
    CHECK(s.names() == std::vector<std::string>{"B", "C", "A"});
    CHECK(s.song.moveSection(0, -1) == 0);
}

TEST_CASE("lengthening or shortening a section moves what comes after it")
{
    Sections s{MODAL_ANY_PPQN()};
    CHECK(s.song.resizeSection(0, 1));
    CHECK(s.song.sectionStart(1) == 3 * s.bar);
    CHECK(s.song.events(s.track)[2].tick == 3 * s.bar); // B's first note, a bar later
    CHECK(s.song.end() == 9 * s.bar);

    // Shortening takes what starts in the bars that go.
    CHECK(s.song.resizeSection(1, -2));
    CHECK(s.song.sectionStart(2) == 4 * s.bar);
    CHECK(s.pitches() == std::vector<int>{40, 41, 42, 45, 46, 47});
    CHECK_FALSE(s.song.resizeSection(1, -1)); // not below a bar
    CHECK_FALSE(s.song.resizeSection(1, 0));
}

TEST_CASE("joining a section with the next makes one of both, with everything where it was")
{
    Sections s{MODAL_ANY_PPQN()};
    const auto bar = s.bar;
    s.song.setMarker(3 * bar, "fill");
    s.song.updateTempo([&](TempoMap& tempo) { tempo.set(3 * bar, Tempo{400'000}); });
    // B has a meter of its own on a second track.
    const auto lead = s.song.addTrack("Lead");
    s.song.updateMeter(lead, [&](MeterMap& meter) { meter.set(2 * bar, TimeSignature{3, 4}); });
    const auto before = s.song.tempo().changes().size();

    CHECK(s.song.joinSections(0));
    CHECK(s.names() == std::vector<std::string>{"A", "C"});
    CHECK(s.song.sectionStart(1) == 5 * bar);
    CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47});
    REQUIRE(s.song.markers().size() == 1);
    CHECK(s.song.markers()[0].tick == 3 * bar);
    CHECK(s.song.tempo().changes().size() == before);
    // The lead keeps its own meter from where B was, following the conductor's before.
    CHECK(s.song.meter(lead).signatureAt(0) == TimeSignature{4, 4});
    CHECK(s.song.meter(lead).signatureAt(2 * bar) == TimeSignature{3, 4});
    CHECK_FALSE(s.song.joinSections(1)); // the last has none after it
}
