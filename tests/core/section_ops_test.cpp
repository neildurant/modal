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
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C", "B (2)"});
    CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47, 42, 43, 44});
    CHECK(s.song.insertSections(0, chorus));
    CHECK(s.names().front() == "B (3)");
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

TEST_CASE("removing bars on every track closes up after them, each section staying one")
{
    Sections s{MODAL_ANY_PPQN()};
    const auto bar = s.bar;

    SECTION("inside a section, which is joined up again")
    {
        const auto edit = s.song.removeBars(3 * bar, 1); // bar 4, in B
        CHECK(edit.done);
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
        CHECK(s.pitches() == std::vector<int>{40, 41, 42, 44, 45, 46, 47});
        CHECK(s.song.sectionStart(2) == 4 * bar);
        CHECK(s.song.end() == 7 * bar);
        // What went, as a section named after the one it came from.
        REQUIRE(edit.removed.size() == 1);
        CHECK(edit.removed[0]->name() == "From B");
        CHECK(edit.removed[0]->length() == bar);
        REQUIRE(edit.removed[0]->content(s.track) != nullptr);
        CHECK(std::get<Note>(edit.removed[0]->content(s.track)->events[0].data).pitch == 43);
    }
    SECTION("at a section's start, which keeps its name")
    {
        CHECK(s.song.removeBars(2 * bar, 2).done); // bars 3 and 4, B's first two
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
        CHECK(s.pitches() == std::vector<int>{40, 41, 44, 45, 46, 47});
        CHECK(s.song.sectionStart(2) == 3 * bar);
    }
    SECTION("across sections, and whole ones")
    {
        const auto edit = s.song.removeBars(4 * bar, 2); // B's last bar and C's first
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
        CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 46, 47});
        CHECK(s.song.sectionStart(2) == 4 * bar);
        REQUIRE(edit.removed.size() == 2);
        CHECK(edit.removed[0]->name() == "From B");
        CHECK(edit.removed[1]->name() == "From C");
        CHECK(s.song.removeBars(0, 2).done); // all of A
        CHECK(s.names() == std::vector<std::string>{"B", "C"});
    }
    SECTION("as far as the song's end, but never all of it, and only from a bar line")
    {
        CHECK(s.song.removeBars(7 * bar, 5).done);
        CHECK(s.song.end() == 7 * bar);
        const auto before = s.song;
        CHECK_FALSE(s.song.removeBars(0, 7).done);
        CHECK_FALSE(s.song.removeBars(bar / 2, 1).done);
        CHECK_FALSE(s.song.removeBars(0, 0).done);
        CHECK(s.song == before);
    }
}

TEST_CASE("removing bars takes the tempo and meter changes and markers in them")
{
    Sections s{MODAL_ANY_PPQN()};
    const auto bar = s.bar;
    const auto q = bar / 4;
    s.song.updateTempo(
        [&](TempoMap& tempo)
        {
            tempo.set(3 * bar + q, Tempo{400'000}); // in bar 4
            tempo.set(4 * bar, Tempo{450'000});     // bar 5's start
        });
    s.song.setMarker(3 * bar, "gone");
    s.song.setMarker(4 * bar + q, "kept");
    s.song.removeBars(3 * bar, 1);
    const auto changes = s.song.tempo().changes();
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].tick == 3 * bar);
    CHECK(changes[1].tempo == Tempo{450'000});
    REQUIRE(s.song.markers().size() == 1);
    CHECK(s.song.markers()[0].name == "kept");
    CHECK(s.song.markers()[0].tick == 3 * bar + q);

    // A bar of 3/4 taken out, the meter after it goes on: here A's last bar, left a 1/4
    // remainder by the change.
    s.song.updateMeter(
        [&](MeterMap& meter)
        {
            meter.set(bar, TimeSignature{3, 4});
            meter.set(bar + 3 * q, TimeSignature{4, 4});
        });
    const auto end = s.song.end();
    CHECK(s.song.removeBars(bar, 1).done);
    CHECK(s.song.end() == end - 3 * q);
    CHECK(s.song.meter().signatureAt(bar) != TimeSignature{3, 4});
    CHECK(s.song.sectionStart(1) == bar + q);
}

TEST_CASE("bars on every track have to line up with the bars of tracks with their own meter")
{
    Sections s{MODAL_ANY_PPQN()};
    const auto bar = s.bar;
    const auto q = bar / 4;
    const auto lead = s.song.addTrack("Lead");
    // In B, from bar 3, the lead is in 3/4, whose bars meet the conductor's every three bars.
    s.song.updateMeter(lead, [&](MeterMap& meter) { meter.set(2 * bar, TimeSignature{3, 4}); });
    const auto before = s.song;

    auto edit = s.song.removeBars(2 * bar, 1);
    CHECK_FALSE(edit.done);
    CHECK(edit.misaligned == lead);
    CHECK(s.song == before);
    edit = s.song.insertBars(3 * bar, 1);
    CHECK_FALSE(edit.done);
    CHECK(edit.misaligned == lead);
    CHECK(s.song == before);

    // Three bars of 4/4 are four of 3/4, so they go in, the lead keeping its 3/4 through them.
    CHECK(s.song.insertBars(2 * bar, 3).done);
    CHECK(s.song.meter(lead).signatureAt(2 * bar) == TimeSignature{3, 4});
    CHECK(s.song.meter(lead).signatureAt(5 * bar + q) == TimeSignature{3, 4});
    CHECK(s.song.removeBars(2 * bar, 3).done);
    CHECK(s.song == before);

    // A meter that lines up, 2/4 under 4/4, never stands in the way.
    s.song.updateMeter(lead, [&](MeterMap& meter) { meter.set(2 * bar, TimeSignature{2, 4}); });
    CHECK(s.song.removeBars(3 * bar, 1).done);
}

TEST_CASE("inserting empty bars on every track moves what's after them on, in the section there")
{
    Sections s{MODAL_ANY_PPQN()};
    const auto bar = s.bar;
    const auto q = bar / 4;

    SECTION("inside a section")
    {
        CHECK(s.song.insertBars(3 * bar, 2).done); // before bar 4, in B
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
        CHECK(s.pitches() == std::vector<int>{40, 41, 42, 43, 44, 45, 46, 47});
        CHECK(s.song.events(s.track)[3].tick == 5 * bar);
        CHECK(s.song.sectionStart(2) == 7 * bar);
        CHECK(s.song.end() == 10 * bar);
    }
    SECTION("at a section's start, into that section")
    {
        CHECK(s.song.insertBars(2 * bar, 1).done);
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
        CHECK(s.song.sectionStart(1) == 2 * bar);
        CHECK(s.song.sectionStart(2) == 6 * bar);
        CHECK(s.song.events(s.track)[2].tick == 3 * bar);
    }
    SECTION("in a meter given, the meter after going on as before")
    {
        CHECK(s.song.insertBars(3 * bar, 1, TimeSignature{3, 4}).done);
        CHECK(s.song.meter().signatureAt(3 * bar) == TimeSignature{3, 4});
        CHECK(s.song.meter().signatureAt(3 * bar + 3 * q) == TimeSignature{4, 4});
        CHECK(s.song.events(s.track)[3].tick == 3 * bar + 3 * q);
    }
    SECTION("only before a bar line in the song")
    {
        const auto before = s.song;
        CHECK_FALSE(s.song.insertBars(q, 1).done);
        CHECK_FALSE(s.song.insertBars(s.song.end(), 1).done);
        CHECK_FALSE(s.song.insertBars(0, 0).done);
        CHECK(s.song == before);
    }
}
