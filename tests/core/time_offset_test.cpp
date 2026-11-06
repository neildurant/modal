// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/playback_plan.hpp>
#include <modal/core/song_file.hpp>
#include <modal/core/time_offset.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace modal::core;
using namespace std::chrono_literals;

TEST_CASE("track offsets parse explicit units and signed coarse plus fine values")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto step = NoteFraction{1, 16}.toTicks(ppqn);
    CHECK(parseTimeOffset("-40ms", ppqn) == TimeOffset{-40ms});
    CHECK(parseTimeOffset("+120t", ppqn) == TimeOffset::ticks(120));
    CHECK(parseTimeOffset("+1/16", ppqn)->amount() == step);
    CHECK(parseTimeOffset("+1/16-22t", ppqn)->amount() == step - 22);
    CHECK(parseTimeOffset("-1/16+22t", ppqn)->amount() == -step + 22);
    CHECK(parseTimeOffset("1/8t-1t", ppqn)->amount() ==
          NoteFraction{1, 8}.triplet().toTicks(ppqn) - 1);
    // The same ticks, given another way, are another offset, shown as given.
    CHECK(parseTimeOffset("+1/16", ppqn) != TimeOffset::ticks(step));
    for (const auto text : {"+1/16", "+1/16-33t", "-1/16+22t", "+1/32.+27t", "+1/8t-1t", "+120t"})
    {
        CHECK(offsetText(*parseTimeOffset(text, ppqn)) == text);
    }
    for (const auto bad :
         {"", "1", "ms", "t", "+-1t", "--1/16", "1/0", "1/16+2ms", "1/16-2", "1/16+2t+3t", "1001ms",
          "-1001ms", "9223372036854775807t", "1/16+9223372036854775807t"})
    {
        INFO(bad);
        CHECK_FALSE(parseTimeOffset(bad, ppqn));
    }
    for (const auto text : {"0t", "0ms", "+1/16-22t", "-40ms"})
    {
        const auto offset = parseTimeOffset(text, ppqn);
        REQUIRE(offset);
        CHECK(parseTimeOffset(offsetText(*offset), ppqn) == offset);
    }
}

TEST_CASE("musical track offsets follow tempo changes and invert for recorded input")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    TempoMap tempo{ppqn, Tempo{500000}};
    tempo.set(q, Tempo{1000000});
    const auto musical = TimeOffset::ticks(q / 2);
    CHECK(musical.at(tempo, 0) == 250ms);
    CHECK(musical.at(tempo, q) == 1s);
    CHECK(TimeOffset{250ms}.at(tempo, q) == 750ms);
    for (const auto offset : {musical, TimeOffset::ticks(-q), TimeOffset{-40ms}})
    {
        for (const auto tick : {Tick{0}, Tick{1}, q - 1, q, 2 * q + 1})
        {
            CHECK(offset.undo(tempo, offset.at(tempo, tick)) == tick);
        }
    }

    Song song{ppqn};
    song.updateTempo([&](TempoMap& into) { into = tempo; });
    const auto id = song.addTrack("Lead");
    song.updateTrack(id, [&](Track& t) { t.timeOffset = TimeOffset::ticks(-q / 2); });
    song.updateEvents(id, [&](EventList& events) { events.insert({q, Note{60, 100, q}}); });
    const auto plan = compilePlan(song, [](const Track&) { return DestinationId{1}; });
    REQUIRE(plan.events.size() == 1);
    CHECK(plan.events[0].time == 250ms);
    CHECK(plan.events[0].noteOffTime == 1s);
    CHECK(song.events(id)[0].tick == q);
}

TEST_CASE("song files preserve offset units and rescale musical offsets")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto id = song.addTrack("Lead");
    for (auto offset :
         {TimeOffset{-40ms}, TimeOffset::ticks(-ppqn.quarter() / 2), TimeOffset::ticks(0)})
    {
        song.updateTrack(id, [&](Track& t) { t.timeOffset = offset; });
        const auto text = writeSong(song);
        const auto loaded = readSong(text, ppqn);
        INFO(loaded.error);
        REQUIRE(loaded.song);
        CHECK(loaded.song->tracks()[0]->timeOffset == offset);
        const auto other = ppqn == kPpqn ? Ppqn{96} : kPpqn;
        const auto rescaled = readSong(text, other);
        REQUIRE(rescaled.song);
        CHECK(rescaled.song->tracks()[0]->timeOffset.musical() == offset.musical());
        CHECK(rescaled.song->tracks()[0]->timeOffset.amount() ==
              (offset.musical() ? offset.amount() * other.value / ppqn.value : offset.amount()));
    }
}

TEST_CASE("song files keep a musical offset as it was given")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto id = song.addTrack("Lead");
    const auto offset = *parseTimeOffset("+1/16-3t", ppqn);
    song.updateTrack(id, [&](Track& t) { t.timeOffset = offset; });
    const auto text = writeSong(song);
    CHECK(text.find("offset=+1/16-3t") != std::string::npos);
    const auto loaded = readSong(text, ppqn);
    REQUIRE(loaded.song);
    CHECK(loaded.song->tracks()[0]->timeOffset == offset);
}

TEST_CASE("changing PPQN keeps an offset's fraction and scales its fine ticks")
{
    const auto offset = *parseTimeOffset("+1/16-48t", kPpqn);
    const auto lower = offset.rescaled(kPpqn, Ppqn{96});
    CHECK(offsetText(lower) == "+1/16-5t");
    CHECK(lower.amount() == NoteFraction{1, 16}.toTicks(Ppqn{96}) - 5);
    CHECK(offsetText(TimeOffset::ticks(480).rescaled(kPpqn, Ppqn{96})) == "+48t");
    CHECK(offsetText(TimeOffset{-40ms}.rescaled(kPpqn, Ppqn{96})) == "-40ms");
}

TEST_CASE("whole tracks keep settings and per-section own or inherited meters")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto bar = ppqn.whole();
    Track original{TrackId{1}};
    original.name = "Bass";
    original.port = "Synth";
    original.channel = 9;
    original.mute = true;
    original.solo = true;
    original.arm = true;
    original.thru = false;
    original.chaseNotes = false;
    original.transpose = -12;
    original.velocityOffset = 15;
    original.timeOffset = TimeOffset::ticks(-ppqn.quarter());
    original.grid = NoteFraction{1, 8}.triplet();
    original.noteLength = NoteFraction{1, 4};
    original.noteVelocity = 70;
    Section intro{"Intro", bar, MeterMap{ppqn}, Tempo{500000}};
    Section verse{"Verse", 2 * bar, MeterMap{ppqn}, Tempo{500000}};
    TrackContent content;
    content.events.insert({0, Note{60, 100, ppqn.quarter()}});
    intro.setContent(original.id(), content);
    content.meter.emplace(ppqn, TimeSignature{2, 4});
    verse.setContent(original.id(), content);
    Song song{ppqn, {original}, {intro, verse}};
    const auto copy = song.copyTrack(original.id());
    REQUIRE(copy);
    const auto duplicate = song.insertTrack(1, *copy);
    REQUIRE(duplicate);
    CHECK(*duplicate != original.id());
    auto expected = original.withId(*duplicate);
    expected.name = "Bass (2)";
    CHECK(*song.track(*duplicate) == expected);
    for (const auto& section : song.sections())
    {
        CHECK(*section->content(original.id()) == *section->content(*duplicate));
    }
    const auto again = song.insertTrack(0, *copy);
    REQUIRE(again);
    CHECK(song.track(*again)->name == "Bass (3)");
    song.removeTrack(original.id());
    CHECK(song.insertTrack(0, *copy));

    song.moveSection(0, 1);
    const auto before = song;
    CHECK_FALSE(song.insertTrack(0, *copy));
    CHECK(song == before);
}

TEST_CASE("an empty track's musical advance has enough lead-in at slower tempos")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Song song{ppqn};
    const auto id = song.addTrack();
    song.updateTrack(id,
                     [&](Track& track) { track.timeOffset = TimeOffset::ticks(-ppqn.quarter()); });
    song.updateTempo([&](TempoMap& tempo) { tempo.set(ppqn.whole(), Tempo{1000000}); });
    const auto plan = compilePlan(song, [](const Track&) { return DestinationId{1}; });
    CHECK(plan.leadIn == 1s);
}
