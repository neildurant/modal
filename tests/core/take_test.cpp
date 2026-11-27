// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/document.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song.hpp>
#include <modal/core/song_file.hpp>
#include <modal/core/takes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace modal::core;

namespace
{

// Eight bars of 4/4 in one section, and a track whose part has take 3 over the section, as entered
// material makes, with a note at bar 1, and over bars 3 to 5, take 2 playing (a note at the start
// of each bar) and take 1 kept beside it.
struct Takes
{
    explicit Takes(Ppqn ppqn)
        : song{ppqn}
        , bar{ppqn.whole()}
        , track{song.addTrack("Bass")}
    {
        const bool done = song.updatePart(0, track,
                                          [&](TrackContent& part)
                                          {
                                              Take base{3, 0, 8 * bar, 0, {}, 2};
                                              base.events.insert({0, note(30)});
                                              part.takes.push_back(base);
                                              part.takes.push_back(take(1, 50, 1000));
                                              part.takes.push_back(take(2, 60, 2000));
                                          });
        REQUIRE(done);
    }

    Note note(int pitch) const { return Note{static_cast<std::uint8_t>(pitch), 100, bar / 4}; }

    // A take over bars 3 to 5, a note of a pitch from it at the start of each bar.
    Take take(std::uint32_t id, int pitch, std::int64_t recorded) const
    {
        Take made;
        made.id = id;
        made.from = 2 * bar;
        made.to = 5 * bar;
        made.recorded = recorded;
        for (int i = 0; i < 3; ++i)
        {
            made.events.insert({(2 + i) * bar, note(pitch + i)});
        }
        return made;
    }

    const TrackContent& part(std::size_t section = 0) const
    {
        const auto* content = song.sections()[section]->content(track);
        REQUIRE(content != nullptr);
        return *content;
    }

    // Each event's tick, in bars, and take.
    std::vector<std::pair<Tick, std::uint32_t>> tags() const
    {
        std::vector<std::pair<Tick, std::uint32_t>> found;
        for (const auto& event : song.events(track).all())
        {
            const auto section = song.sectionAt(event.tick);
            const auto* content = song.sections()[section]->content(track);
            REQUIRE(content);
            found.emplace_back(event.tick / bar,
                               activeSource(*content, event.tick - song.sectionStart(section)));
        }
        return found;
    }

    Song song;
    Tick bar;
    TrackId track;
};

using Tags = std::vector<std::pair<Tick, std::uint32_t>>;

} // namespace

TEST_CASE("a track's part keeps its takes and derives the source from the active span")
{
    Takes t{MODAL_ANY_PPQN()};
    REQUIRE(t.part().takes.size() == 3);
    CHECK(t.part().take(2)->recorded == 2000);
    CHECK(t.part().take(4) == nullptr);
    CHECK(t.song.nextTakeId() == 4);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 2}, {4, 2}});

    SECTION("a put goes into the take playing there")
    {
        REQUIRE(t.song.updateEvents(t.track, [&](EventList& events)
                                    { events.insert({7 * t.bar, t.note(70)}); }));
        CHECK(t.tags().back() == std::pair<Tick, std::uint32_t>{7, 3});
    }
    SECTION("a take outside the section is refused")
    {
        const auto before = t.song;
        CHECK_FALSE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                                      { part.takes.back().to = 9 * t.bar; }));
        CHECK(t.song.sections()[0] == before.sections()[0]);
    }
}

TEST_CASE("moving an event into another section edits the active source at the destination")
{
    Takes t{MODAL_ANY_PPQN()};
    t.song.splitSection(5 * t.bar, "After"); // after the take, which it has none of
    t.song.updateEvents(t.track,
                        [&](EventList& events)
                        {
                            auto moved = events[1]; // bar 3, from take 2
                            events.erase(1);
                            moved.tick = 6 * t.bar;
                            events.insert(moved);
                        });
    CHECK(t.tags() == Tags{{0, 3}, {3, 2}, {4, 2}, {6, 3}});
}

TEST_CASE("splitting a section through a take cuts it, and joining puts it back together")
{
    Takes t{MODAL_ANY_PPQN()};
    const auto whole = t.part();
    REQUIRE(t.song.splitSection(3 * t.bar, "Second"));

    const auto& first = t.part(0);
    REQUIRE(first.takes.size() == 3);
    CHECK(first.takes[0].to == 3 * t.bar); // the take entered material made, cut too
    CHECK(first.takes[2].from == 2 * t.bar);
    CHECK(first.takes[2].to == 3 * t.bar);
    CHECK(first.takes[2].events.size() == 1);
    const auto& second = t.part(1);
    REQUIRE(second.takes.size() == 3);
    CHECK(second.takes[0].id == 3);
    CHECK(second.takes[2].id == 2);
    CHECK(second.takes[2].from == 0);
    CHECK(second.takes[2].to == 2 * t.bar);
    CHECK(second.takes[2].events.size() == 2);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 2}, {4, 2}});

    REQUIRE(t.song.joinSections(0));
    CHECK(t.part() == whole);
}

TEST_CASE("takes follow bars removed and inserted, and a shortened section cuts them")
{
    Takes t{MODAL_ANY_PPQN()};
    SECTION("removing a bar in a take's span shortens it, closing up")
    {
        REQUIRE(t.song.removeBars(3 * t.bar, 1).done);
        const auto& take = t.part().takes[2];
        CHECK(take.from == 2 * t.bar);
        CHECK(take.to == 4 * t.bar);
        CHECK(take.events.size() == 2);
        CHECK(take.events[1].tick == 3 * t.bar);
        CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 2}});
    }
    SECTION("inserting bars in a take's span widens it over them")
    {
        REQUIRE(t.song.insertBars(3 * t.bar, 2).done);
        const auto& take = t.part().takes[2];
        CHECK(take.from == 2 * t.bar);
        CHECK(take.to == 7 * t.bar);
        CHECK(take.events[1].tick == 5 * t.bar);
        CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {5, 2}, {6, 2}});
    }
    SECTION("removing every bar of a take's span loses it")
    {
        REQUIRE(t.song.removeBars(2 * t.bar, 3).done);
        REQUIRE(t.part().takes.size() == 1);
        CHECK(t.part().takes[0].id == 3); // the take under them, closed up
        CHECK(t.part().takes[0].to == 5 * t.bar);
    }
    SECTION("shortening the section to bar 4 cuts the takes there")
    {
        REQUIRE(t.song.resizeSection(0, -4));
        REQUIRE(t.part().takes.size() == 3);
        CHECK(t.part().takes[0].to == 4 * t.bar);
        CHECK(t.part().takes[1].to == 4 * t.bar);
        CHECK(t.part().takes[1].events.size() == 2);
    }
}

TEST_CASE("joining sections keeps both parts' takes apart, numbering again those that clash")
{
    Takes t{MODAL_ANY_PPQN()};
    REQUIRE(t.song.insertEmptySection(1, "Next", 8));
    REQUIRE(t.song.updatePart(1, t.track,
                              [&](TrackContent& part)
                              {
                                  part.takes.push_back(t.take(2, 80, 3000)); // another take 2
                                  assignTake(part, {2 * t.bar, 3 * t.bar, 2});
                              }));
    REQUIRE(t.song.joinSections(0));
    const auto& part = t.part();
    REQUIRE(part.takes.size() == 4);
    CHECK(part.takes[3].id == 4);
    CHECK(part.takes[3].recorded == 3000);
    CHECK(part.takes[3].from == 10 * t.bar);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 2}, {4, 2}, {10, 4}, {11, 4}, {12, 4}});
    CHECK(part.comp == std::vector{TakeRange{10 * t.bar, 11 * t.bar, 4}});
}

TEST_CASE("take stacks and active ranges round trip without event tags")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Takes t{ppqn};
    REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                              { assignTake(part, {3 * t.bar, 4 * t.bar, 1}); }));
    const auto text = writeSong(t.song);
    CHECK(text.starts_with("modal-song 2\n"));
    CHECK(text.find(" take=") == std::string::npos);
    CHECK(text.find(" ppqn=" + std::to_string(ppqn.value) + " next-take=4\n") != std::string::npos);
    CHECK(text.find("    take 1 3:1:0 6:1:0 recorded=1000 colour=1\n"
                    "      3:1:0 note D3 v=100 len=1/4\n") != std::string::npos);
    CHECK(text.find("    comp 1 4:1:0 5:1:0\n") != std::string::npos);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 1}, {4, 2}});
    const auto visible = activeTakeRanges(t.part(), t.bar, 6 * t.bar);
    CHECK(visible == std::vector{TakeRange{t.bar, 2 * t.bar, 3}, TakeRange{2 * t.bar, 3 * t.bar, 2},
                                 TakeRange{3 * t.bar, 4 * t.bar, 1},
                                 TakeRange{4 * t.bar, 5 * t.bar, 2},
                                 TakeRange{5 * t.bar, 6 * t.bar, 3}});

    const auto read = readSong(text, ppqn);
    REQUIRE(read.song);
    REQUIRE(read.song->sections()[0]->content(t.track) != nullptr);
    CHECK(*read.song->sections()[0]->content(t.track) == t.part());
    CHECK(read.song->events(t.track) == t.song.events(t.track));
    CHECK(writeSong(*read.song) == text);
}

TEST_CASE("takes are rescaled with the song")
{
    Takes t{kPpqn};
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  const auto end = t.song.end();
                                  Take last{4, end - 1, end, 0, {}, 2};
                                  last.events.insert({end - 1, ControlChange{11, 90}});
                                  part.takes.push_back(std::move(last));
                              }));
    const auto read = readSong(writeSong(t.song), Ppqn{96});
    REQUIRE(read.song);
    const auto* part = read.song->sections()[0]->content(t.track);
    REQUIRE(part);
    const auto& take = part->takes[2];
    CHECK(take.from == 2 * Ppqn{96}.whole());
    CHECK(take.to == 5 * Ppqn{96}.whole());
    CHECK(take.events[2].tick == 4 * Ppqn{96}.whole());
    CHECK(part->takes.back().from == read.song->end() - 1);
    CHECK(part->takes.back().events[0].tick == read.song->end() - 1);
    CHECK(part->validTakes(read.song->end()));
}

TEST_CASE("the reader rejects old take files and invalid comp ranges")
{
    const std::string body = "song \"\" ppqn=960\ntrack 1 \"Bass\" ch=1\n"
                             "section \"A\" bars=4\n  meter 1:1:0 4/4\n  tempo 1:1:0 120\n"
                             "  track 1\n";
    const auto error = [&](const std::string& lines)
    { return readSong("modal-song 2\n" + body + lines, kPpqn).error; };
    const std::string take = "    take 1 2:1:0 3:1:0\n";

    CHECK(readSong("modal-song 1\n" + body + take, kPpqn).error.find("old take format") !=
          std::string::npos);
    CHECK(error("    1:1:0 note C4 v=100 len=1/4 take=1\n").find("old take format") !=
          std::string::npos);
    CHECK(error(take + "      1:1:0 note C4 v=100 len=1/4\n") ==
          "line 9: the event is outside its take");
    CHECK(error(take + "    take 1 3:1:0 4:1:0\n") == "line 9: take 1 comes twice in this track");
    CHECK(error("    take 1 3:1:0 2:1:0\n") ==
          "line 8: a take's span runs forwards, within its section");
    CHECK(error(take + "    comp 2 2:1:0 3:1:0\n").find("missing take") != std::string::npos);
    CHECK(error(take + "    comp 1 1:1:0 3:1:0\n").find("outside its span") != std::string::npos);
    CHECK(
        error(take + "    comp 1 2:1:0 3:1:0\n    comp 1 2:1:0 3:1:0\n").find("without overlaps") !=
        std::string::npos);
}

TEST_CASE("choosing a take replaces events starting in its span and leaves held notes")
{
    Takes t{MODAL_ANY_PPQN()};
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  // A long note from bar 2 into the takes' span, and one after.
                                  auto& base = part.takes.front();
                                  base.events.insert({t.bar, Note{30, 100, 2 * t.bar}});
                                  base.events.insert({6 * t.bar, t.note(70)});
                              }));
    CHECK(playingTake(t.part(), 3 * t.bar) == 2u);

    REQUIRE(chooseTake(t.song, 0, t.track, 1));
    CHECK(t.tags() == Tags{{0, 3}, {1, 3}, {2, 1}, {3, 1}, {4, 1}, {6, 3}});
    const auto& events = t.song.events(t.track);
    CHECK(std::get<Note>(events[1].data).duration == 2 * t.bar);
    CHECK(std::get<Note>(events[2].data).pitch == 50);
    CHECK(playingTake(t.part(), 3 * t.bar) == 1u);
    CHECK(t.part().takes.size() == 3); // the takes themselves unchanged

    const auto before = t.song;
    CHECK_FALSE(chooseTake(t.song, 0, t.track, 9));
    CHECK_FALSE(chooseTake(t.song, 1, t.track, 1));
    CHECK(t.song.sections()[0] == before.sections()[0]);
}

TEST_CASE("the newest span plays by default, even where it is silent")
{
    Takes t{MODAL_ANY_PPQN()};
    // A silent span over bar 4, within take 2's span.
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  Take punch;
                                  punch.id = 4;
                                  punch.from = 3 * t.bar;
                                  punch.to = 4 * t.bar;
                                  part.takes.push_back(punch);
                              }));
    CHECK(playingTake(t.part(), 3 * t.bar) == 4u);
    CHECK(playingTake(t.part(), 2 * t.bar) == 2u);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {4, 2}});
    CHECK(playingTake(t.part(), 0) == 3u);
}

TEST_CASE("editing the playing part updates its selected takes and keeps hidden material")
{
    Takes t{MODAL_ANY_PPQN()};
    const auto original = t.part().takes[1];
    const auto at = 2 * t.bar;
    REQUIRE(t.song.updateEvents(t.track,
                                [&](EventList& events)
                                {
                                    events.erase(1);
                                    events.insert({at, t.note(80)});
                                    events.insert({at, ControlChange{11, 64}});
                                }));
    CHECK(t.part().takes[1] == original);
    CHECK(t.part().takes[2].events[0] == Event{at, ControlChange{11, 64}});
    CHECK(t.part().takes[2].events[1] == Event{at, t.note(80)});
    REQUIRE(chooseTake(t.song, 0, t.track, 1));
    CHECK(t.song.events(t.track)[1] == original.events[0]);
}

TEST_CASE("a saved comp feeds the scheduler, chase and MIDI export from the same sources")
{
    Takes t{MODAL_ANY_PPQN()};
    const auto at = 2 * t.bar;
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  part.takes[1].events.insert({at, ControlChange{11, 90}});
                                  part.takes[2].events.insert({at, ControlChange{11, 10}});
                                  assignTake(part, {at, 3 * t.bar, 1});
                              }));
    const auto loaded = readSong(writeSong(t.song), t.song.ppqn());
    REQUIRE(loaded.song);
    const auto plan = compilePlan(*loaded.song, [](auto) { return DestinationId{1}; });
    const auto state = chase(plan, 4 * t.bar);
    REQUIRE(state.size() == 1);
    const auto bytes = state.front().message.view();
    REQUIRE(bytes.size() == 3);
    CHECK(bytes[1] == 11);
    CHECK(bytes[2] == 90);

    const auto exported = exportSmf(*loaded.song);
    REQUIRE(exported.error.empty());
    const auto midi = importSmf(exported.bytes);
    REQUIRE(midi.song);
    const auto atMidiResolution = readSong(writeSong(t.song), midi.song->ppqn());
    REQUIRE(atMidiResolution.song);
    CHECK(midi.song->events(midi.song->tracks().front()->id()) ==
          atMidiResolution.song->events(t.track));
}

TEST_CASE("keeping only the playing take preserves the audible part")
{
    Takes t{MODAL_ANY_PPQN()};
    REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                              { assignTake(part, {3 * t.bar, 4 * t.bar, 1}); }));
    CHECK(keepOnlyTake(t.song, 0, t.track, 2) == 2);
    REQUIRE(t.part().takes.size() == 1);
    CHECK(t.part().takes.front().id == 2);
    CHECK(t.part().takes.front().from == 0);
    CHECK(t.part().takes.front().to == 8 * t.bar);
    // What was heard from the others is now in the take kept.
    CHECK(t.tags() == Tags{{0, 2}, {2, 2}, {3, 2}, {4, 2}});
    std::vector<int> pitches;
    for (const auto& event : t.song.events(t.track).all())
    {
        pitches.push_back(std::get<Note>(event.data).pitch);
    }
    CHECK(pitches == std::vector<int>{30, 60, 51, 62});
    CHECK(keepOnlyTake(t.song, 0, t.track, 2) == 0);
    CHECK(keepOnlyTake(t.song, 0, t.track, 1) == 0);
}

TEST_CASE("discarding a take clears its whole performance and chooses a neighbouring take")
{
    Takes t{MODAL_ANY_PPQN()};
    SECTION("a shorter preceding take leaves no events from the discarded take outside its span")
    {
        REQUIRE(t.song.updatePart(0, t.track,
                                  [&](TrackContent& part)
                                  {
                                      auto& first = part.takes[1];
                                      first.to = 3 * t.bar;
                                      first.events = {};
                                      first.events.insert({2 * t.bar, t.note(50)});
                                      part.takes.front().events.insert({4 * t.bar, t.note(70)});
                                  }));
        const auto discarded = discardTake(t.song, 0, t.track, 2);
        CHECK(discarded.done);
        CHECK(discarded.instead == 1u);
        CHECK(t.tags() == Tags{{0, 3}, {2, 1}, {4, 3}});
    }
    SECTION(
        "discarding the first chooses the next, then discarding the last reveals the take under it")
    {
        REQUIRE(chooseTake(t.song, 0, t.track, 1));
        const auto first = discardTake(t.song, 0, t.track, 1);
        CHECK(first.done);
        CHECK(first.instead == 2u);
        CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 2}, {4, 2}});
        const auto last = discardTake(t.song, 0, t.track, 2);
        CHECK(last.done);
        CHECK(last.instead == 3u);
        CHECK(t.tags() == Tags{{0, 3}});
        CHECK_FALSE(discardTake(t.song, 0, t.track, 2).done);
    }
}

namespace
{

// Eight bars over two sections of four, Verse and Chorus, with a track, Keys.
struct TwoSections
{
    explicit TwoSections(Ppqn ppqn)
        : song{ppqn}
        , bar{ppqn.whole()}
        , track{song.addTrack("Keys")}
    {
        REQUIRE(song.splitSection(4 * bar, "Chorus"));
        REQUIRE(song.renameSection(0, "Verse"));
    }

    Note note(int pitch) const { return Note{static_cast<std::uint8_t>(pitch), 100, bar / 4}; }

    // A replace pass with a note at the start of each of its bars.
    void record(Song& into, Tick from, Tick to, int pitch, std::int64_t recorded) const
    {
        EventList played;
        for (Tick at = from; at < to; at += bar)
        {
            played.insert({at, note(pitch)});
        }
        into.addRecordedTake(track, from, to, recorded, played);
    }

    const TrackContent& part(const Song& in, std::size_t section) const
    {
        const auto* content = in.sections()[section]->content(track);
        REQUIRE(content != nullptr);
        return *content;
    }

    std::vector<std::uint32_t> ids(const Song& in, std::size_t section) const
    {
        std::vector<std::uint32_t> found;
        for (const auto& take : part(in, section).takes)
        {
            found.push_back(take.id);
        }
        return found;
    }

    std::vector<int> colours(const Song& in, std::size_t section) const
    {
        std::vector<int> found;
        for (const auto& take : part(in, section).takes)
        {
            found.push_back(take.colour);
        }
        return found;
    }

    Song song;
    Tick bar;
    TrackId track;
};

using Ids = std::vector<std::uint32_t>;
using Colours = std::vector<int>;

} // namespace

TEST_CASE("a pass across sections is one take, numbered after what it replaced")
{
    TwoSections t{MODAL_ANY_PPQN()};
    // Entered by hand in the Verse, under the pass; the Chorus has nothing before it.
    REQUIRE(t.song.updateEvents(t.track, [&](EventList& events)
                                { events.insert({2 * t.bar, t.note(50)}); }));
    t.record(t.song, 2 * t.bar, 6 * t.bar, 60, 1000);

    CHECK(t.ids(t.song, 0) == Ids{1, 2});
    CHECK(t.ids(t.song, 1) == Ids{2});
    CHECK(t.part(t.song, 1).takes[0].recorded == 1000);
    // One colour for the pass in both sections, after the first take's.
    CHECK(t.colours(t.song, 0) == Colours{0, 1});
    CHECK(t.colours(t.song, 1) == Colours{1});
    CHECK(t.song.nextTakeId() == 3);

    SECTION("joining the sections puts the pass back together")
    {
        REQUIRE(t.song.joinSections(0));
        CHECK(t.ids(t.song, 0) == Ids{1, 2});
        CHECK(t.part(t.song, 0).takes[1].from == 2 * t.bar);
        CHECK(t.part(t.song, 0).takes[1].to == 6 * t.bar);
        CHECK(t.song.nextTakeId() == 3);
    }
}

TEST_CASE("take numbers are never used again, even after undo")
{
    TwoSections t{MODAL_ANY_PPQN()};
    Document document{t.song};
    const auto record = [&](int pitch)
    {
        REQUIRE(document.edit("Record", {},
                              [&](Song& song) { t.record(song, 0, 2 * t.bar, pitch, 1000); }));
    };
    record(60);
    record(62);
    CHECK(t.ids(document.song(), 0) == Ids{1, 2});

    REQUIRE(document.undo());
    CHECK(t.ids(document.song(), 0) == Ids{1});
    record(64);
    CHECK(t.ids(document.song(), 0) == Ids{1, 3});

    REQUIRE(document.undo());
    REQUIRE(document.undo());
    CHECK(document.song().nextTakeId() == 4);
    record(65);
    CHECK(t.ids(document.song(), 0) == Ids{4});

    SECTION("nor after saving and reading the song again")
    {
        REQUIRE(document.edit("Discard", {},
                              [&](Song& song) { REQUIRE(discardTake(song, 0, t.track, 4).done); }));
        const auto text = writeSong(document.song());
        CHECK(text.find(" next-take=5\n") != std::string::npos);
        const auto read = readSong(text, document.song().ppqn());
        REQUIRE(read.song);
        CHECK(read.song->nextTakeId() == 5);
    }
}

TEST_CASE("copied sections get new take numbers, the pieces of a take sharing one")
{
    TwoSections t{MODAL_ANY_PPQN()};
    REQUIRE(t.song.updateEvents(t.track, [&](EventList& events)
                                { events.insert({2 * t.bar, t.note(50)}); }));
    t.record(t.song, 2 * t.bar, 6 * t.bar, 60, 1000);
    REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                              { assignTake(part, {3 * t.bar, 4 * t.bar, 1}); }));
    const auto original = t.song.sections()[0];

    const std::vector copied(t.song.sections().begin(), t.song.sections().end());
    REQUIRE(t.song.insertSections(2, copied));
    CHECK(t.ids(t.song, 2) == Ids{3, 4});
    CHECK(t.ids(t.song, 3) == Ids{4});
    CHECK(t.part(t.song, 2).comp ==
          std::vector{TakeRange{2 * t.bar, 3 * t.bar, 4}, TakeRange{3 * t.bar, 4 * t.bar, 3}});
    CHECK(t.part(t.song, 3).comp == std::vector{TakeRange{0, 2 * t.bar, 4}});
    CHECK(t.colours(t.song, 2) == t.colours(t.song, 0));
    CHECK(t.song.nextTakeId() == 5);
    // The originals are as they were.
    CHECK(t.song.sections()[0] == original);
    CHECK(t.ids(t.song, 1) == Ids{2});
}

TEST_CASE("a take keeps its colour as others go, and a new one takes the free colour")
{
    TwoSections t{MODAL_ANY_PPQN()};
    t.record(t.song, 0, 2 * t.bar, 60, 1000);
    t.record(t.song, 0, 2 * t.bar, 62, 2000);
    t.record(t.song, 0, 2 * t.bar, 64, 3000);
    CHECK(t.ids(t.song, 0) == Ids{1, 2, 3});
    CHECK(t.colours(t.song, 0) == Colours{0, 1, 2});

    REQUIRE(discardTake(t.song, 0, t.track, 2).done);
    CHECK(t.colours(t.song, 0) == Colours{0, 2});
    t.record(t.song, 0, 2 * t.bar, 65, 4000);
    CHECK(t.ids(t.song, 0) == Ids{1, 3, 4});
    CHECK(t.colours(t.song, 0) == Colours{0, 2, 1});

    SECTION("saved with the song")
    {
        const auto text = writeSong(t.song);
        CHECK(text.find("take 4 1:1:0 3:1:0 recorded=4000 colour=2\n") != std::string::npos);
        const auto read = readSong(text, t.song.ppqn());
        REQUIRE(read.song);
        CHECK(t.colours(*read.song, 0) == Colours{0, 2, 1});
    }
    SECTION("or, saved without them, given as if made in order")
    {
        auto text = writeSong(t.song);
        for (const auto* colour : {" colour=1", " colour=2", " colour=3"})
        {
            for (auto at = text.find(colour); at != std::string::npos; at = text.find(colour))
            {
                text.erase(at, std::string_view{colour}.size());
            }
        }
        const auto read = readSong(text, t.song.ppqn());
        REQUIRE(read.song);
        CHECK(t.colours(*read.song, 0) == Colours{0, 1, 2});
    }
    SECTION("a colour that isn't one is refused")
    {
        auto text = writeSong(t.song);
        const auto at = text.find("colour=2");
        REQUIRE(at != std::string::npos);
        text.replace(at, 8, "colour=5");
        CHECK_FALSE(readSong(text, t.song.ppqn()).song);
    }
}

TEST_CASE("the take trial fixture reads, its numbers gaps and all")
{
    std::ifstream file{std::string{MODAL_FIXTURE_DIR} + "/takes.modal", std::ios::binary};
    REQUIRE(file);
    const std::string text{std::istreambuf_iterator<char>{file}, {}};
    const auto read = readSong(text, kPpqn);
    REQUIRE(read.song);
    CHECK(read.song->nextTakeId() == 13);
    const auto ids = [&](std::size_t track)
    {
        std::vector<std::uint32_t> found;
        const auto* part = read.song->sections()[0]->content(read.song->tracks()[track]->id());
        REQUIRE(part);
        for (const auto& take : part->takes)
        {
            found.push_back(take.id);
        }
        return found;
    };
    CHECK(ids(0) == std::vector<std::uint32_t>{3, 5, 8});
    CHECK(ids(1) == std::vector<std::uint32_t>{11});
    // Only Keys in the Verse has a choice; Bass's take is alone, and the Outro is plain.
    const auto* keys = read.song->sections()[0]->content(read.song->tracks()[0]->id());
    const auto* bass = read.song->sections()[0]->content(read.song->tracks()[1]->id());
    const auto* outro = read.song->sections()[1]->content(read.song->tracks()[0]->id());
    REQUIRE(outro);
    CHECK(hasComping(*keys));
    CHECK_FALSE(hasComping(*bass));
    REQUIRE(outro->takes.size() == 1);
    CHECK(outro->takes[0].id == 12);
    CHECK_FALSE(hasComping(*outro));
    const auto written = writeSong(*read.song);
    CHECK(written.find("  track 1 take=12 colour=1\n    1:1:0 note C4 v=80 len=1/2\n") !=
          std::string::npos);
}

TEST_CASE("material entered into a part without a take makes one over the section")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto bar = ppqn.whole();
    Song song{ppqn};
    const auto track = song.addTrack("Keys");
    REQUIRE(song.splitSection(4 * bar, "Chorus"));
    const auto before = song.nextTakeId();
    REQUIRE(song.updateEvents(track, [&](EventList& events)
                              { events.insert({bar, Note{60, 100, bar / 4}}); }));
    const auto* part = song.sections()[0]->content(track);
    REQUIRE(part);
    REQUIRE(part->takes.size() == 1);
    CHECK(part->takes[0].id == before);
    CHECK(part->takes[0].from == 0);
    CHECK(part->takes[0].to == 4 * bar);
    CHECK(part->takes[0].colour == 0);
    CHECK_FALSE(hasComping(*part));
    CHECK(song.sections()[1]->content(track) == nullptr); // nothing went into the Chorus

    SECTION("more material there goes into the same take")
    {
        REQUIRE(song.updateEvents(track, [&](EventList& events)
                                  { events.insert({3 * bar, Note{62, 100, bar / 4}}); }));
        REQUIRE(song.sections()[0]->content(track)->takes.size() == 1);
        CHECK(song.sections()[0]->content(track)->takes[0].events.size() == 2);
    }
    SECTION("removing it all leaves the part empty again")
    {
        REQUIRE(song.updateEvents(track, [](EventList& events) { events.erase(0); }));
        CHECK(song.sections()[0]->content(track) == nullptr);
        CHECK(song.nextTakeId() == before + 1); // its number isn't used again
    }
}

TEST_CASE("material in a gap goes into the take before it, or the one after if none comes before")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto bar = ppqn.whole();
    Song song{ppqn};
    const auto track = song.addTrack("Keys");
    const auto entered = [&](Tick at)
    {
        REQUIRE(song.updateEvents(track, [&](EventList& events)
                                  { events.insert({at, Note{60, 100, bar / 4}}); }));
    };
    const auto spans = [&]
    {
        std::vector<std::tuple<std::uint32_t, Tick, Tick, std::size_t>> found;
        for (const auto& take : song.sections()[0]->content(track)->takes)
        {
            found.emplace_back(take.id, take.from / bar, take.to / bar, take.events.size());
        }
        return found;
    };
    using Spans = std::vector<std::tuple<std::uint32_t, Tick, Tick, std::size_t>>;
    // A recording over bars 3 and 4 alone, the rest of the eight bars a gap.
    EventList played;
    played.insert({2 * bar, Note{64, 100, bar / 4}});
    song.addRecordedTake(track, 2 * bar, 4 * bar, 1000, played);
    REQUIRE(spans() == Spans{{1, 2, 4, 1}});

    SECTION("after it, the take grows over the gap")
    {
        entered(6 * bar);
        CHECK(spans() == Spans{{1, 2, 8, 2}});
        CHECK_FALSE(hasComping(*song.sections()[0]->content(track)));
    }
    SECTION("before it, with nothing earlier, the take grows back over the gap")
    {
        entered(0);
        CHECK(spans() == Spans{{1, 0, 4, 2}});
    }
    SECTION("between two takes, the one before grows up to the next")
    {
        EventList later;
        later.insert({6 * bar, Note{67, 100, bar / 4}});
        song.addRecordedTake(track, 6 * bar, 7 * bar, 2000, later);
        entered(5 * bar);
        CHECK(spans() == Spans{{1, 2, 6, 2}, {2, 6, 7, 1}});
    }
}

TEST_CASE("material entered where none is chosen starts a take there, chosen for it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Takes t{ppqn};
    REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                              { assignTake(part, {3 * t.bar, 4 * t.bar, 0}); }));
    REQUIRE(t.tags() == Tags{{0, 3}, {2, 2}, {4, 2}}); // bar 4 silent
    REQUIRE(t.song.updateEvents(t.track, [&](EventList& events)
                                { events.insert({3 * t.bar, t.note(90)}); }));
    const auto& part = t.part();
    REQUIRE(part.takes.size() == 4);
    CHECK(part.takes.back().id == 4);
    CHECK(part.takes.back().from == 3 * t.bar);
    CHECK(part.takes.back().to == 4 * t.bar);
    CHECK(t.tags() == Tags{{0, 3}, {2, 2}, {3, 4}, {4, 2}});
}

TEST_CASE("a song read or imported without takes has one take for each part")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const std::string text = "modal-song 1\n"
                             "song \"Plain\" ppqn=" +
                             std::to_string(ppqn.value) +
                             " next-take=7\n"
                             "track 1 \"Keys\" ch=1\n"
                             "track 2 \"Bass\" ch=2\n"
                             "section \"A\" bars=2\n"
                             "  meter 1:1:0 4/4\n"
                             "  tempo 1:1:0 120\n"
                             "  track 1\n"
                             "    1:1:0 note C4 v=100 len=1/4\n"
                             "  track 2 take=4 colour=3\n"
                             "    2:1:0 note C2 v=100 len=1/4\n";
    const auto read = readSong(text, ppqn);
    REQUIRE(read.song);
    const auto& section = *read.song->sections()[0];
    const auto* keys = section.content(read.song->tracks()[0]->id());
    const auto* bass = section.content(read.song->tracks()[1]->id());
    REQUIRE(keys);
    REQUIRE(bass);
    REQUIRE(keys->takes.size() == 1);
    CHECK(keys->takes[0].id == 7); // a new number, past those the song has used
    CHECK(keys->takes[0].to == section.length());
    REQUIRE(bass->takes.size() == 1);
    CHECK(bass->takes[0].id == 4);
    CHECK(bass->takes[0].colour == 2);
    CHECK(read.song->nextTakeId() == 8);

    // Written plainly again, each with its take, and read back the same.
    const auto written = writeSong(*read.song);
    CHECK(written.starts_with("modal-song 1\n"));
    CHECK(written.find("  track 1 take=7 colour=1\n") != std::string::npos);
    CHECK(written.find("  track 2 take=4 colour=3\n") != std::string::npos);
    const auto again = readSong(written, ppqn);
    REQUIRE(again.song);
    const auto& reread = *again.song->sections()[0];
    CHECK(*reread.content(read.song->tracks()[0]->id()) == *keys);
    CHECK(*reread.content(read.song->tracks()[1]->id()) == *bass);

    SECTION("events in a part with take lines are refused")
    {
        auto withTake = text + "    take 9 1:1:0 2:1:0\n";
        withTake.replace(0, std::string_view{"modal-song 1"}.size(), "modal-song 2");
        const auto mixed = readSong(withTake, ppqn);
        CHECK(mixed.error.find("a part with take lines has its events in them") !=
              std::string::npos);
    }
    SECTION("an imported MIDI file's parts each have a take")
    {
        const auto exported = exportSmf(*read.song);
        REQUIRE(exported.error.empty());
        const auto midi = importSmf(exported.bytes);
        REQUIRE(midi.song);
        for (const auto& track : midi.song->tracks())
        {
            const auto* part = midi.song->sections()[0]->content(track->id());
            REQUIRE(part);
            CHECK(part->takes.size() == 1);
            CHECK_FALSE(hasComping(*part));
        }
    }
}
