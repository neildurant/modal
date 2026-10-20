#include "support/edit_track.hpp"
#include "support/ppqn.hpp"

#include <modal/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace modal::core;
using namespace modal::test;

namespace
{

std::vector<Tick> ticksOf(const EventList& events)
{
    std::vector<Tick> ticks;
    for (const auto& event : events.all())
    {
        ticks.push_back(event.tick);
    }
    return ticks;
}

std::vector<MeterChange> changesOf(const MeterMap& meter)
{
    return {meter.changes().begin(), meter.changes().end()};
}

} // namespace

TEST_CASE("a new song")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Song song{ppqn};

    CHECK(song.ppqn() == ppqn);
    CHECK(song.tempo().ppqn() == ppqn);
    CHECK(song.tempo().tempoAt(0) == kDefaultTempo);
    CHECK(song.meter().signatureAt(0) == kDefaultTimeSignature);
    CHECK(song.tracks().empty());
    CHECK(song.markers().empty());
    CHECK(Song{}.ppqn() == kPpqn);

    // One section of eight bars.
    REQUIRE(song.sections().size() == 1);
    const auto& section = *song.sections()[0];
    CHECK(section.name() == "Song");
    CHECK(section.length() == 8 * ppqn.whole());
    CHECK(song.sectionStart(0) == 0);
    CHECK(song.end() == 8 * ppqn.whole());
}

TEST_CASE("tracks have stable IDs")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto drums = song.addTrack("Drums");
    const auto bass = song.addTrack("Bass");
    const auto keys = song.addTrack(1, "Keys");

    REQUIRE(song.tracks().size() == 3);
    CHECK(song.tracks()[0]->name == "Drums");
    CHECK(song.tracks()[1]->name == "Keys");
    CHECK(song.tracks()[2]->name == "Bass");
    CHECK(song.events(bass).empty());
    CHECK(song.meter(bass) == song.meter());

    CHECK(song.removeTrack(keys));
    CHECK_FALSE(song.removeTrack(keys));
    CHECK(song.track(keys) == nullptr);
    CHECK(song.track(drums)->name == "Drums");

    // IDs are never reused.
    const auto strings = song.addTrack("Strings");
    CHECK(strings != keys);
    CHECK(strings != drums);
    CHECK(strings != bass);
}

TEST_CASE("editing a copy leaves the original and its other tracks alone")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song original{ppqn};
    const auto drums = original.addTrack("Drums");
    const auto bass = original.addTrack("Bass");
    original.splitSection(4 * ppqn.whole(), "Verse");
    original.updateEvents(drums, [&](EventList& events)
                          { events.insert({0, Note{36, 100, ppqn.quarter()}}); });

    Song edited = original;
    REQUIRE(edited.updateTrack(bass, [](Track& track) { track.transpose = -12; }));
    REQUIRE(edited.updateEvents(bass, [&](EventList& events)
                                { events.insert({5 * ppqn.whole(), Note{40, 100, 1}}); }));

    CHECK(original.events(bass).empty());
    CHECK(original.track(bass)->transpose == 0);
    CHECK(edited.events(bass).size() == 1);
    CHECK(edited.track(bass)->transpose == -12);

    // What wasn't touched is shared, not copied: the drums, and the first section.
    CHECK(original.track(drums) == edited.track(drums));
    CHECK(original.track(bass) != edited.track(bass));
    CHECK(original.sections()[0] == edited.sections()[0]);
    CHECK(original.sections()[1] != edited.sections()[1]);

    CHECK_FALSE(edited.updateTrack(TrackId{999}, [](Track&) {}));
    CHECK_FALSE(edited.updateEvents(TrackId{999}, [](EventList&) {}));
}

TEST_CASE("events are held by the section they start in, from its start")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar = ppqn.whole();
    Song song{ppqn};
    const auto piano = song.addTrack("Piano");
    song.splitSection(2 * bar, "Verse");
    song.updateEvents(piano,
                      [&](EventList& events)
                      {
                          events.insert({0, Note{60, 100, ppqn.quarter()}});
                          events.insert({bar + ppqn.quarter(), Note{62, 100, bar}}); // into Verse
                          events.insert({2 * bar, ControlChange{64, 127}});
                          events.insert({3 * bar + 1, Note{64, 100, 1}});
                      });

    const auto& intro = *song.sections()[0];
    const auto& verse = *song.sections()[1];
    REQUIRE(intro.content(piano));
    REQUIRE(verse.content(piano));
    CHECK(ticksOf(intro.content(piano)->events) == std::vector<Tick>{0, bar + ppqn.quarter()});
    CHECK(ticksOf(verse.content(piano)->events) == std::vector<Tick>{0, bar + 1});

    // The flattened events are the song's, from its start, as given.
    CHECK(ticksOf(song.events(piano)) ==
          std::vector<Tick>{0, bar + ppqn.quarter(), 2 * bar, 3 * bar + 1});

    // A track with nothing in a section, following the conductor, has no content there.
    song.updateEvents(piano, [](EventList& events) { events.erase(0); });
    CHECK(song.sections()[0]->content(piano) != nullptr);
    song.updateEvents(piano, [](EventList& events) { events.erase(0); });
    CHECK(song.sections()[0]->content(piano) == nullptr);
}

TEST_CASE("anything put past the end grows the last section by whole bars")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto piano = song.addTrack("Piano");
    song.updateMeter([](MeterMap& meter) { meter.set(0, {3, 4}); });
    const Tick bar34 = 3 * ppqn.quarter();
    REQUIRE(song.end() == 11 * bar34); // 8 bars of 4/4, growing to the next 3/4 bar line

    // A note starting inside but ending past the end.
    song.updateEvents(piano, [&](EventList& events)
                      { events.insert({10 * bar34, Note{60, 100, bar34 + 1}}); });
    CHECK(song.end() == 12 * bar34);
    REQUIRE(song.sections().size() == 1);
    CHECK(song.sections()[0]->length() == 12 * bar34);

    song.setMarker(20 * bar34, "Later");
    CHECK(song.end() == 21 * bar34);

    song.updateTempo([&](TempoMap& tempo) { tempo.set(30 * bar34 + 1, Tempo{1'000'000}); });
    CHECK(song.end() == 31 * bar34);
}

TEST_CASE("fitting the last section to what's in it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar = ppqn.whole();
    Song song{ppqn};
    song.fitToContent();
    CHECK(song.end() == bar); // at least one bar

    const auto piano = song.addTrack("Piano");
    song.updateEvents(piano,
                      [&](EventList& events) { events.insert({2 * bar, Note{60, 100, bar}}); });
    song.fitToContent();
    CHECK(song.end() == 3 * bar);
    song.updateEvents(piano,
                      [&](EventList& events) { events.insert({2 * bar, Note{62, 100, bar + 1}}); });
    song.fitToContent();
    CHECK(song.end() == 4 * bar); // the end of the longer note
}

TEST_CASE("tracks follow the conductor's meter unless they have their own")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    const auto drums = song.addTrack("Drums");
    const auto lead = song.addTrack("Lead");

    song.updateMeter([&](MeterMap& meter) { meter.set(4 * q, {3, 4}); });
    CHECK(song.meter(drums) == song.meter());
    CHECK(song.meter(lead) == song.meter());

    REQUIRE(
        song.updateMeter(lead, [](MeterMap& meter) { meter = MeterMap{meter.ppqn(), {7, 8}}; }));
    CHECK(song.meter(lead).signatureAt(4 * q) == TimeSignature{7, 8});
    CHECK(song.meter(drums).signatureAt(4 * q) == TimeSignature{3, 4});
    REQUIRE(song.sections()[0]->content(lead));
    CHECK(song.sections()[0]->content(lead)->meter);

    // A conductor change reaches the tracks following it, not those with their own.
    song.updateMeter([&](MeterMap& meter) { meter.set(4 * q, {5, 4}); });
    CHECK(song.meter(drums).signatureAt(4 * q) == TimeSignature{5, 4});
    CHECK(song.meter(lead).signatureAt(4 * q) == TimeSignature{7, 8});

    REQUIRE(song.followConductor(lead));
    CHECK(song.meter(lead) == song.meter());
    CHECK(song.sections()[0]->content(lead) == nullptr);
}

TEST_CASE("a section keeps its length when the conductor's meter changes, but the last grows")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    Song song{ppqn};
    song.splitSection(16 * q, "Verse"); // after four bars of 4/4

    song.updateMeter([](MeterMap& meter) { meter.set(0, {3, 4}); });

    // Four bars of 4/4 are five bars of 3/4 and a 1/4 remainder, which the section keeps.
    const auto& intro = *song.sections()[0];
    CHECK(intro.length() == 16 * q);
    CHECK(changesOf(intro.meter()) == std::vector<MeterChange>{{0, {3, 4}}, {15 * q, {1, 4}}});

    // The last, sixteen beats from bar 5, grows to the next 3/4 bar line.
    CHECK(song.sections()[1]->length() == 18 * q);
    CHECK(song.end() == 34 * q);

    // Bars start again at the boundary: bar 7 is the Verse's first.
    CHECK(song.meter().barStart(7) == 16 * q);
    CHECK(song.meter().signatureAt(16 * q) == TimeSignature{3, 4});
}

TEST_CASE("splitting a section")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick bar = ppqn.whole();
    Song song{ppqn};
    const auto drums = song.addTrack("Drums");
    const auto lead = song.addTrack("Lead");
    song.updateMeter(lead, [](MeterMap& meter) { meter = MeterMap{meter.ppqn(), {7, 8}}; });
    song.updateTempo([&](TempoMap& tempo) { tempo.set(2 * bar, Tempo{400'000}); });
    song.setMarker(5 * bar, "fill");
    song.updateEvents(drums,
                      [&](EventList& events)
                      {
                          events.insert({0, Note{36, 100, q}});
                          events.insert({4 * bar, Note{38, 100, q}});
                      });

    SECTION("at a bar line of every track")
    {
        // Seven bars of 4/4 are eight of 7/8, so both have a bar line there.
        const auto split = song.splitSection(7 * bar, "Verse");
        REQUIRE(split);
        CHECK(split->index == 1);
        CHECK_FALSE(split->conductor);
        CHECK(split->tracks.empty());
    }

    SECTION("mid-bar on some tracks")
    {
        const auto split = song.splitSection(4 * bar, "Verse");
        REQUIRE(split);
        CHECK_FALSE(split->conductor);
        CHECK(split->tracks == std::vector<TrackId>{lead});

        REQUIRE(song.sections().size() == 2);
        const auto& intro = *song.sections()[0];
        const auto& verse = *song.sections()[1];
        CHECK(intro.name() == "Song");
        CHECK(verse.name() == "Verse");
        CHECK(intro.length() == 4 * bar);
        CHECK(verse.length() == 4 * bar);
        CHECK(song.sectionStart(1) == 4 * bar);
        CHECK(song.end() == 8 * bar);

        // Each keeps its own tempo at its start.
        CHECK(intro.tempo().size() == 2);
        REQUIRE(verse.tempo().size() == 1);
        CHECK(verse.tempo()[0] == TempoChange{0, Tempo{400'000}});
        CHECK(song.tempo().changes().size() == 2); // with no repeat at the boundary

        // The marker and events move to the Verse, positioned from its start.
        CHECK(intro.markers().empty());
        REQUIRE(verse.markers().size() == 1);
        CHECK(verse.markers()[0] == Marker{bar, "fill"});
        CHECK(song.markers()[0] == Marker{5 * bar, "fill"});
        CHECK(ticksOf(verse.content(drums)->events) == std::vector<Tick>{0});
        CHECK(ticksOf(song.events(drums)) == std::vector<Tick>{0, 4 * bar});

        // Four whole notes are four 7/8 bars and a 1/2 remainder; then the Verse starts a new
        // 7/8 bar.
        const Tick bar78 = 7 * ppqn.whole() / 8;
        CHECK(changesOf(*intro.content(lead)->meter) ==
              std::vector<MeterChange>{{0, {7, 8}}, {4 * bar78, {4, 8}}});
        CHECK(song.meter(lead).barStart(6) == 4 * bar);
        CHECK(song.meter(lead).signatureAt(4 * bar) == TimeSignature{7, 8});
    }

    SECTION("mid-bar on the conductor")
    {
        const auto split = song.splitSection(4 * bar + 2 * q, "Verse");
        REQUIRE(split);
        CHECK(split->conductor);
        CHECK(changesOf(song.sections()[0]->meter()) ==
              std::vector<MeterChange>{{0, {4, 4}}, {4 * bar, {2, 4}}});
        CHECK(song.meter().barStart(6) == 4 * bar + 2 * q);

        // The rest, three and a half bars, grows to four, since it's the last section.
        CHECK(song.sections()[1]->length() == 4 * bar);
        CHECK(song.end() == 8 * bar + 2 * q);
    }

    SECTION("names are numbered if taken")
    {
        song.splitSection(2 * bar, "Chorus");
        song.splitSection(4 * bar, "Chorus");
        song.splitSection(6 * bar, "Chorus");
        REQUIRE(song.sections().size() == 4);
        CHECK(song.sections()[1]->name() == "Chorus");
        CHECK(song.sections()[2]->name() == "Chorus 2");
        CHECK(song.sections()[3]->name() == "Chorus 3");
    }

    SECTION("renaming")
    {
        song.splitSection(4 * bar, "Verse");
        CHECK(song.renameSection(0, "Intro"));
        CHECK(song.sections()[0]->name() == "Intro");
        CHECK(song.renameSection(0, "Intro")); // its own name isn't taken
        CHECK(song.sections()[0]->name() == "Intro");
        CHECK(song.renameSection(0, "Verse"));
        CHECK(song.sections()[0]->name() == "Verse 2");
        CHECK_FALSE(song.renameSection(2, "None"));
    }

    SECTION("only inside a section, not at its start")
    {
        CHECK_FALSE(song.splitSection(0, "No"));
        CHECK_FALSE(song.splitSection(song.end(), "No"));
        REQUIRE(song.splitSection(4 * bar, "Verse"));
        CHECK_FALSE(song.splitSection(4 * bar, "No"));
        CHECK(song.sections().size() == 2);
    }
}

TEST_CASE("markers")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar = ppqn.whole();
    Song song{ppqn};
    song.splitSection(4 * bar, "Chorus");

    song.setMarker(16 * bar, "Chorus");
    song.setMarker(8 * bar, "Verse");
    song.setMarker(16 * bar, "Chorus 1");

    REQUIRE(song.markers().size() == 2);
    CHECK(song.markers()[0] == Marker{8 * bar, "Verse"});
    CHECK(song.markers()[1] == Marker{16 * bar, "Chorus 1"});
    CHECK(song.end() == 17 * bar); // grown to hold the second

    // Names are unique within a section, not across sections.
    song.setMarker(2 * bar, "Verse");
    song.setMarker(9 * bar, "Verse");
    CHECK(song.markers()[0] == Marker{2 * bar, "Verse"});
    CHECK(song.markers()[2] == Marker{9 * bar, "Verse 2"});

    CHECK(song.removeMarker(8 * bar));
    CHECK_FALSE(song.removeMarker(8 * bar));
    CHECK(song.markers().size() == 3);
}

TEST_CASE("removing a track removes its content")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto piano = song.addTrack("Piano");
    song.updateEvents(piano, [](EventList& events) { events.insert({0, Note{60, 100, 1}}); });
    REQUIRE(song.sections()[0]->content(piano));
    song.removeTrack(piano);
    CHECK(song.sections()[0]->content(piano) == nullptr);
}

TEST_CASE("editing a track's settings and events together")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto piano = song.addTrack("Piano");
    editTrack(song, piano,
              [](EditTrack& track)
              {
                  track.mute = true;
                  track.events.insert({0, Note{60, 100, 1}});
              });
    CHECK(song.track(piano)->mute);
    CHECK(song.events(piano).size() == 1);
}

TEST_CASE("a song made from its parts")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick q = ppqn.quarter();
    const Tick bar = ppqn.whole();

    Track bass{TrackId{3}};
    bass.name = "Bass";
    EventList events;
    events.insert({0, Note{40, 100, q}});

    Section intro{"Intro", 2 * bar, MeterMap{ppqn}, kDefaultTempo};
    intro.addTempo({bar, Tempo{400'000}});
    intro.addMarker({bar, "fill"});
    intro.setContent(TrackId{3}, {std::nullopt, events});
    Section verse{"Verse", 6 * q, MeterMap{ppqn, {3, 4}}, Tempo{400'000}};
    verse.setColour("blue");
    verse.setContent(TrackId{3}, {std::nullopt, events});

    const Song song{ppqn, {bass}, {intro, verse}};
    REQUIRE(song.tracks().size() == 1);
    CHECK(song.tracks()[0]->name == "Bass");
    CHECK(song.sectionStart(1) == 2 * bar);
    CHECK(song.end() == 2 * bar + 6 * q);
    CHECK(song.sections()[1]->colour() == "blue");
    CHECK(ticksOf(song.events(TrackId{3})) == std::vector<Tick>{0, 2 * bar});
    CHECK(song.tempo().changes().size() == 2); // the verse's tempo repeats the one before
    CHECK(song.markers()[0] == Marker{bar, "fill"});
    CHECK(song.meter(TrackId{3}).signatureAt(2 * bar) == TimeSignature{3, 4});

    // Equal to a copy until one changes; new tracks get IDs after the highest.
    Song copy = song;
    CHECK(copy == song);
    CHECK(copy.addTrack("Keys") == TrackId{4});
    CHECK(copy != song);
    copy = song;
    copy.renameSection(1, "Chorus");
    CHECK(copy != song);
}
