// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

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
                                              for (const auto& event : part.takes[1].events.all())
                                              {
                                                  part.events.insert({event.tick, event.data, 2});
                                              }
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
            found.emplace_back(event.tick / bar, event.take);
        }
        return found;
    }

    Song song;
    Tick bar;
    TrackId track;
};

using Tags = std::vector<std::pair<Tick, std::uint32_t>>;

} // namespace

TEST_CASE("a track's part keeps its takes, and the events playing note the take they came from")
{
    Takes t{MODAL_ANY_PPQN()};
    REQUIRE(t.part().takes.size() == 2);
    CHECK(t.part().take(2)->recorded == 2000);
    CHECK(t.part().take(3) == nullptr);
    CHECK(t.part().nextTakeId() == 3);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}});

    SECTION("an event noting a take the part doesn't have notes none")
    {
        REQUIRE(t.song.updatePart(0, t.track, [&](TrackContent& part)
                                  { part.events.insert({7 * t.bar, t.note(70), 9}); }));
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

TEST_CASE("an event moved into another section forgets its take")
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
                                  part.events.insert({2 * t.bar, t.note(80), 2});
                              }));
    REQUIRE(t.song.joinSections(0));
    const auto& part = t.part();
    REQUIRE(part.takes.size() == 3);
    CHECK(part.takes[2].id == 3);
    CHECK(part.takes[2].recorded == 3000);
    CHECK(part.takes[2].from == 10 * t.bar);
    CHECK(t.tags() == Tags{{0, 0}, {2, 2}, {3, 2}, {4, 2}, {10, 3}});
}

TEST_CASE("takes are saved with the song, Before among them, and read back the same")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Takes t{ppqn};
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  auto before = t.take(3, 40, 0);
                                  before.before = true;
                                  part.takes.insert(part.takes.begin(), before);
                              }));
    const auto text = writeSong(t.song);
    CHECK(text.find("3:1:0 note C4 v=100 len=1/4 take=2\n") != std::string::npos);
    CHECK(text.find("    take 3 before 3:1:0 6:1:0\n") != std::string::npos);
    CHECK(text.find("    take 1 3:1:0 6:1:0 recorded=1000\n      3:1:0 note D3 v=100 len=1/4\n") !=
          std::string::npos);

    const auto read = readSong(text, ppqn);
    REQUIRE(read.song);
    CHECK(read.song->sections()[0]->content(t.track) != nullptr);
    CHECK(*read.song->sections()[0]->content(t.track) == t.part());
    CHECK(writeSong(*read.song) == text);
}

TEST_CASE("takes are rescaled with the song")
{
    Takes t{kPpqn};
    const auto read = readSong(writeSong(t.song), Ppqn{96});
    REQUIRE(read.song);
    const auto& take = read.song->sections()[0]->content(t.track)->takes[1];
    CHECK(take.from == 2 * Ppqn{96}.whole());
    CHECK(take.to == 5 * Ppqn{96}.whole());
    CHECK(take.events[2].tick == 4 * Ppqn{96}.whole());
}

TEST_CASE("a take or a take= that doesn't make sense stops the song being read, saying why")
{
    const std::string head = "modal-song 1\nsong \"\" ppqn=960\ntrack 1 \"Bass\" ch=1\n"
                             "section \"A\" bars=4\n  meter 1:1:0 4/4\n  tempo 1:1:0 120\n"
                             "  track 1\n";
    const auto error = [&](const std::string& lines)
    { return readSong(head + lines, kPpqn).error; };

    CHECK(error("    1:1:0 note C4 v=100 len=1/4 take=1\n") ==
          "line 9: an event names take 1, which the track doesn't have here");
    CHECK(error("    take 1 2:1:0 3:1:0\n      1:1:0 note C4 v=100 len=1/4\n") ==
          "line 9: the event is outside its take");
    CHECK(error("    take 1 2:1:0 3:1:0\n    take 1 3:1:0 4:1:0\n") ==
          "line 9: take 1 comes twice in this track");
    CHECK(error("    take 1 3:1:0 2:1:0\n") ==
          "line 8: a take's span runs forwards, within its section");
    CHECK(error("    take 1 2:1:0 3:1:0\n      2:1:0 note C4 v=100 len=1/4 take=1\n") ==
          "line 9: an event in a take doesn't name one");
    CHECK(error("    take 1 2:1:0 3:1:0 when=3\n") == "line 8: unknown take setting \"when=3\"");
}

TEST_CASE("choosing a take puts it in place of what starts in its span, noting it")
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
    CHECK(std::get<Note>(events[1].data).duration == t.bar); // cut where the span starts
    CHECK(std::get<Note>(events[2].data).pitch == 50);
    CHECK(playingTake(t.part(), 3 * t.bar) == 1u);
    CHECK(t.part().takes.size() == 2); // the takes themselves unchanged

    const auto before = t.song;
    CHECK_FALSE(chooseTake(t.song, 0, t.track, 9));
    CHECK_FALSE(chooseTake(t.song, 1, t.track, 1));
    CHECK(t.song.sections()[0] == before.sections()[0]);
}

TEST_CASE("the take playing is the one noted whose span holds the time, or the last noted")
{
    Takes t{MODAL_ANY_PPQN()};
    // A punch of bar 4 alone, chosen, within take 2's span.
    REQUIRE(t.song.updatePart(0, t.track,
                              [&](TrackContent& part)
                              {
                                  Take punch;
                                  punch.id = 3;
                                  punch.from = 3 * t.bar;
                                  punch.to = 4 * t.bar;
                                  punch.events.insert({3 * t.bar, t.note(90)});
                                  part.takes.push_back(punch);
                              }));
    REQUIRE(chooseTake(t.song, 0, t.track, 3));
    CHECK(playingTake(t.part(), 3 * t.bar) == 3u);
    CHECK(playingTake(t.part(), 2 * t.bar) == 2u);
    CHECK(playingTake(t.part(), 0) == 3u); // in no span: the last noted
    REQUIRE(t.song.updatePart(0, t.track, [](TrackContent& part) { part.events = {}; }));
    CHECK_FALSE(playingTake(t.part(), 0));
}
