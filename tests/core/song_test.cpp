#include "support/ppqn.hpp"

#include <modal/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace modal::core;

TEST_CASE("a new song")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Song song{ppqn};

    CHECK(song.ppqn() == ppqn);
    CHECK(song.tempo().ppqn() == ppqn);
    CHECK(song.tracks().empty());
    CHECK(song.markers().empty());
    CHECK_FALSE(song.referenceTrack());
    CHECK(song.referenceMeter().signatureAt(0) == kDefaultTimeSignature);
    CHECK(Song{}.ppqn() == kPpqn);
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
    CHECK(song.track(bass)->meter.ppqn() == ppqn);

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

    Song edited = original;
    REQUIRE(edited.updateTrack(bass,
                               [&](Track& track)
                               {
                                   track.events.insert({0, Note{40, 100, ppqn.quarter()}});
                                   track.transpose = -12;
                               }));

    CHECK(original.track(bass)->events.empty());
    CHECK(original.track(bass)->transpose == 0);
    CHECK(edited.track(bass)->events.size() == 1);
    CHECK(edited.track(bass)->transpose == -12);

    // The untouched track is shared, not copied.
    CHECK(original.track(drums) == edited.track(drums));
    CHECK(original.track(bass) != edited.track(bass));

    CHECK_FALSE(edited.updateTrack(TrackId{999}, [](Track&) {}));
}

TEST_CASE("the reference track defaults to track 1")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto first = song.addTrack("One");
    const auto second = song.addTrack("Two");
    song.updateTrack(second, [](Track& track) { track.meter.set(0, {3, 4}); });

    CHECK(song.referenceTrack() == first);
    CHECK(song.referenceMeter().signatureAt(0) == TimeSignature{4, 4});

    song.setReferenceTrack(second);
    CHECK(song.referenceTrack() == second);
    CHECK(song.referenceMeter().signatureAt(0) == TimeSignature{3, 4});

    // Removing the chosen track falls back to track 1.
    song.removeTrack(second);
    CHECK(song.referenceTrack() == first);

    song.removeTrack(first);
    CHECK_FALSE(song.referenceTrack());
}

TEST_CASE("markers")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const Tick bar = ppqn.whole();
    Song song{ppqn};

    song.setMarker(16 * bar, "Chorus");
    song.setMarker(8 * bar, "Verse");
    song.setMarker(16 * bar, "Chorus 1");

    REQUIRE(song.markers().size() == 2);
    CHECK(song.markers()[0] == Marker{8 * bar, "Verse"});
    CHECK(song.markers()[1] == Marker{16 * bar, "Chorus 1"});

    CHECK(song.removeMarker(8 * bar));
    CHECK_FALSE(song.removeMarker(8 * bar));
    CHECK(song.markers().size() == 1);
}
