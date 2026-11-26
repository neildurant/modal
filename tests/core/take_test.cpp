// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/playback_plan.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song.hpp>
#include <modal/core/song_file.hpp>
#include <modal/core/takes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace modal::core;

namespace
{

// Eight bars of 4/4 in one section, and a track whose part has a note of its own at bar 1 and,
// over bars 3 to 5, take 2 playing (a note at the start of each bar) and take 1 kept beside it.
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
                                              part.events.insert({0, note(30)});
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
    REQUIRE(t.part().takes.size() == 2);
    CHECK(t.part().take(2)->recorded == 2000);
    CHECK(t.part().take(3) == nullptr);
    CHECK(t.part().nextTakeId() == 3);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}});

    SECTION("a put into the underlying part gets its source from the destination")
    {
        REQUIRE(t.song.updateEvents(t.track, [&](EventList& events)
                                    { events.insert({7 * t.bar, t.note(70)}); }));
        CHECK(t.tags().back() == std::pair<Tick, std::uint32_t>{7, 0});
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
    CHECK(t.tags() == Tags{{0, 0}, {3, 2}, {4, 2}, {6, 0}});
}

TEST_CASE("splitting a section through a take cuts it, and joining puts it back together")
{
    Takes t{MODAL_ANY_PPQN()};
    const auto whole = t.part();
    REQUIRE(t.song.splitSection(3 * t.bar, "Second"));

    const auto& first = t.part(0);
    REQUIRE(first.takes.size() == 2);
    CHECK(first.takes[1].from == 2 * t.bar);
    CHECK(first.takes[1].to == 3 * t.bar);
    CHECK(first.takes[1].events.size() == 1);
    const auto& second = t.part(1);
    REQUIRE(second.takes.size() == 2);
    CHECK(second.takes[1].id == 2);
    CHECK(second.takes[1].from == 0);
    CHECK(second.takes[1].to == 2 * t.bar);
    CHECK(second.takes[1].events.size() == 2);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}});

    REQUIRE(t.song.joinSections(0));
    CHECK(t.part() == whole);
}

TEST_CASE("takes follow bars removed and inserted, and a shortened section cuts them")
{
    Takes t{MODAL_ANY_PPQN()};
    SECTION("removing a bar in a take's span shortens it, closing up")
    {
        REQUIRE(t.song.removeBars(3 * t.bar, 1).done);
        const auto& take = t.part().takes[1];
        CHECK(take.from == 2 * t.bar);
        CHECK(take.to == 4 * t.bar);
        CHECK(take.events.size() == 2);
        CHECK(take.events[1].tick == 3 * t.bar);
        CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}});
    }
    SECTION("inserting bars in a take's span widens it over them")
    {
        REQUIRE(t.song.insertBars(3 * t.bar, 2).done);
        const auto& take = t.part().takes[1];
        CHECK(take.from == 2 * t.bar);
        CHECK(take.to == 7 * t.bar);
        CHECK(take.events[1].tick == 5 * t.bar);
        CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {5, 2}, {6, 2}});
    }
    SECTION("removing every bar of a take's span loses it")
    {
        REQUIRE(t.song.removeBars(2 * t.bar, 3).done);
        CHECK(t.part().takes.empty());
    }
    SECTION("shortening the section to bar 4 cuts the takes there")
    {
        REQUIRE(t.song.resizeSection(0, -4));
        REQUIRE(t.part().takes.size() == 2);
        CHECK(t.part().takes[0].to == 4 * t.bar);
        CHECK(t.part().takes[0].events.size() == 2);
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
    REQUIRE(part.takes.size() == 3);
    CHECK(part.takes[2].id == 3);
    CHECK(part.takes[2].recorded == 3000);
    CHECK(part.takes[2].from == 10 * t.bar);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}, {10, 3}, {11, 3}, {12, 3}});
    CHECK(part.comp == std::vector{TakeRange{10 * t.bar, 11 * t.bar, 3}});
}

TEST_CASE("take stacks and active ranges round trip without event tags")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Takes t{ppqn};
    REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                              { assignTake(part, {3 * t.bar, 4 * t.bar, 1}); }));
    const auto text = writeSong(t.song);
    CHECK(text.starts_with("modal-song 2\n"));
    CHECK(text.find("take=") == std::string::npos);
    CHECK(text.find("    take 1 3:1:0 6:1:0 recorded=1000\n      3:1:0 note D3 v=100 len=1/4\n") !=
          std::string::npos);
    CHECK(text.find("    comp 1 4:1:0 5:1:0\n") != std::string::npos);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 1}, {4, 2}});

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
                                  Take last{3, end - 1, end, 0, {}};
                                  last.events.insert({end - 1, ControlChange{11, 90}});
                                  part.takes.push_back(std::move(last));
                              }));
    const auto read = readSong(writeSong(t.song), Ppqn{96});
    REQUIRE(read.song);
    const auto* part = read.song->sections()[0]->content(t.track);
    REQUIRE(part);
    const auto& take = part->takes[1];
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
                                  part.events.insert({t.bar, Note{30, 100, 2 * t.bar}});
                                  part.events.insert({6 * t.bar, t.note(70)});
                              }));
    CHECK(playingTake(t.part(), 3 * t.bar) == 2u);

    REQUIRE(chooseTake(t.song, 0, t.track, 1));
    CHECK(t.tags() == Tags{{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 1}, {6, 0}});
    const auto& events = t.song.events(t.track);
    CHECK(std::get<Note>(events[1].data).duration == 2 * t.bar);
    CHECK(std::get<Note>(events[2].data).pitch == 50);
    CHECK(playingTake(t.part(), 3 * t.bar) == 1u);
    CHECK(t.part().takes.size() == 2); // the takes themselves unchanged

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
                                  punch.id = 3;
                                  punch.from = 3 * t.bar;
                                  punch.to = 4 * t.bar;
                                  part.takes.push_back(punch);
                              }));
    CHECK(playingTake(t.part(), 3 * t.bar) == 3u);
    CHECK(playingTake(t.part(), 2 * t.bar) == 2u);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {4, 2}});
    CHECK_FALSE(playingTake(t.part(), 0));
}

TEST_CASE("editing the playing part updates its selected takes and keeps hidden material")
{
    Takes t{MODAL_ANY_PPQN()};
    const auto original = t.part().takes[0];
    const auto at = 2 * t.bar;
    REQUIRE(t.song.updateEvents(t.track,
                                [&](EventList& events)
                                {
                                    events.erase(1);
                                    events.insert({at, t.note(80)});
                                    events.insert({at, ControlChange{11, 64}});
                                }));
    CHECK(t.part().takes[0] == original);
    CHECK(t.part().takes[1].events[0] == Event{at, ControlChange{11, 64}});
    CHECK(t.part().takes[1].events[1] == Event{at, t.note(80)});
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
                                  part.takes[0].events.insert({at, ControlChange{11, 90}});
                                  part.takes[1].events.insert({at, ControlChange{11, 10}});
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
    CHECK(keepOnlyTake(t.song, 0, t.track, 2) == 1);
    REQUIRE(t.part().takes.size() == 1);
    CHECK(t.part().takes.front().id == 2);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 0}, {4, 2}});
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
                                      auto& first = part.takes.front();
                                      first.to = 3 * t.bar;
                                      first.events = {};
                                      first.events.insert({2 * t.bar, t.note(50)});
                                      part.events.insert({4 * t.bar, t.note(70)});
                                  }));
        const auto discarded = discardTake(t.song, 0, t.track, 2);
        CHECK(discarded.done);
        CHECK(discarded.instead == 1u);
        CHECK(t.tags() == Tags{{0, 0}, {2, 1}, {4, 0}});
    }
    SECTION(
        "discarding the first chooses the next, then discarding the last leaves ordinary events")
    {
        REQUIRE(chooseTake(t.song, 0, t.track, 1));
        const auto first = discardTake(t.song, 0, t.track, 1);
        CHECK(first.done);
        CHECK(first.instead == 2u);
        CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}});
        const auto last = discardTake(t.song, 0, t.track, 2);
        CHECK(last.done);
        CHECK_FALSE(last.instead);
        CHECK(t.tags() == Tags{{0, 0}});
        CHECK_FALSE(discardTake(t.song, 0, t.track, 2).done);
    }
}
