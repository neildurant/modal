// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/ramp.hpp>
#include <modal/core/smf.hpp>
#include <modal/core/song.hpp>
#include <modal/core/song_file.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <utility>
#include <vector>

using namespace modal::core;

namespace
{

constexpr Lane kModulation{.kind = Lane::Kind::Controller, .controller = 1};
constexpr Lane kExpression{.kind = Lane::Kind::Controller, .controller = 11};

std::vector<std::pair<Tick, int>> pointsOf(std::span<const Event> events, const Lane& lane)
{
    std::vector<std::pair<Tick, int>> points;
    for (const auto& event : events)
    {
        if (lane.shows(event.data))
        {
            points.emplace_back(event.tick, lane.value(event.data));
        }
    }
    return points;
}

std::vector<std::pair<Tick, int>> pointsOf(const std::vector<Event>& events,
                                           const Lane& lane = kModulation)
{
    return pointsOf(std::span<const Event>{events}, lane);
}

std::vector<std::pair<Tick, int>> pointsOf(const EventList& events, const Lane& lane = kModulation)
{
    return pointsOf(events.all(), lane);
}

ControllerRamp ramp(Tick start, Tick length, int from, int to, Lane lane = kModulation)
{
    return {.lane = lane, .start = start, .length = length, .from = from, .to = to};
}

Event modulation(Tick tick, int value)
{
    return {tick, ControlChange{1, static_cast<std::uint8_t>(value)}};
}

// A song with one track holding modulation at every beat of its first two bars, and a note.
struct Fixture
{
    Ppqn ppqn = MODAL_ANY_PPQN();
    Tick q = ppqn.quarter();
    Song song{ppqn};
    TrackId id = song.addTrack("Strings");

    Fixture()
    {
        song.updateEvents(id,
                          [&](EventList& events)
                          {
                              for (int beat = 0; beat < 8; ++beat)
                              {
                                  events.insert(modulation(beat * q, 10 + beat));
                              }
                              events.insert({0, Note{60, 100, 8 * q, 64}});
                          });
    }
};

} // namespace

TEST_CASE("a ramp's points are spaced from its start, rounded, and end on its last value")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    auto r = ramp(0, q, 0, 127);
    r.density = 4;
    CHECK(pointsOf(rampEvents(r, ppqn)) ==
          std::vector<std::pair<Tick, int>>{
              {0, 0}, {q / 4, 32}, {q / 2, 64}, {3 * q / 4, 95}, {q, 127}});

    SECTION("a repeated value is left out, but both ends are written")
    {
        auto slow = ramp(0, 4 * q, 10, 11);
        CHECK(pointsOf(rampEvents(slow, ppqn)) ==
              std::vector<std::pair<Tick, int>>{{0, 10}, {2 * q, 11}, {4 * q, 11}});
        slow.to = 10;
        CHECK(pointsOf(rampEvents(slow, ppqn)) ==
              std::vector<std::pair<Tick, int>>{{0, 10}, {4 * q, 10}});
    }
    SECTION("moved, it makes the same points moved")
    {
        auto moved = r;
        moved.start = 3 * q + 1;
        auto events = rampEvents(r, ppqn);
        for (auto& event : events)
        {
            event.tick += 3 * q + 1;
        }
        CHECK(rampEvents(moved, ppqn) == events);
    }
    SECTION("pitch bend and aftertouch ramp too")
    {
        const Lane bend{.kind = Lane::Kind::PitchBend};
        CHECK(pointsOf(rampEvents(ramp(0, q, -8192, 8191, bend), ppqn), bend).back() ==
              std::pair<Tick, int>{q, 8191});
        const Lane pressure{.kind = Lane::Kind::Pressure};
        CHECK(pointsOf(rampEvents(ramp(0, q, 127, 0, pressure), ppqn), pressure).front() ==
              std::pair<Tick, int>{0, 127});
    }
}

TEST_CASE("a ramp's shapes")
{
    const auto q = MODAL_ANY_PPQN().quarter();
    auto r = ramp(0, 4 * q, 0, 100);
    const auto middle = [&] { return r.valueAt(2 * q); };
    CHECK(middle() == 50.0);
    r.shape = ControllerRamp::Shape::Curve;
    r.amount = 50;
    CHECK(middle() < 50.0); // staying near the start, then changing quickly
    r.amount = -50;
    CHECK(middle() > 50.0);
    r.amount = 0;
    CHECK(middle() == 50.0);
    r.shape = ControllerRamp::Shape::SCurve;
    CHECK(middle() == Catch::Approx(50.0));
    CHECK(r.valueAt(q) < 25.0);
    r.shape = ControllerRamp::Shape::EqualPower;
    CHECK(middle() > 70.0);
    std::swap(r.from, r.to);
    CHECK(middle() > 70.0); // a fade out keeps its level longer too
    CHECK(r.valueAt(0) == 100.0);
    CHECK(r.valueAt(4 * q) == Catch::Approx(0.0).margin(1e-9));
}

TEST_CASE("which ramps make sense")
{
    const auto q = kPpqn.quarter();
    CHECK(ramp(0, q, 0, 127).valid());
    CHECK_FALSE(ramp(0, 0, 0, 127).valid());
    CHECK_FALSE(ramp(0, q, 0, 128).valid());
    CHECK_FALSE(ramp(0, q, 0, 127, Lane{}).valid()); // velocity
    CHECK_FALSE(ramp(0, q, 0, 127, Lane{.kind = Lane::Kind::Controller, .controller = 64}).valid());
    CHECK_FALSE(ramp(0, q, 0, 127, Lane{.kind = Lane::Kind::Controller, .controller = 0}).valid());
    auto r = ramp(0, q, 0, 127);
    r.density = 0;
    CHECK_FALSE(r.valid());
    r.density = ControllerRamp::kMaxDensity + 1;
    CHECK_FALSE(r.valid());
    r.density = 16;
    r.amount = 101;
    CHECK_FALSE(r.valid());
    for (const auto shape : {ControllerRamp::Shape::Linear, ControllerRamp::Shape::Curve,
                             ControllerRamp::Shape::SCurve, ControllerRamp::Shape::EqualPower})
    {
        CHECK(parseRampShape(toString(shape)) == shape);
    }
    CHECK_FALSE(parseRampShape("wobbly"));
}

TEST_CASE("a ramp hides its controller's events beneath it without losing them")
{
    Fixture f;
    const auto q = f.q;
    auto& song = f.song;
    const auto before = song.events(f.id);
    auto r = ramp(q, 2 * q, 100, 0);
    r.density = 1;
    const auto added = song.addRamp(f.id, r);
    REQUIRE(added.done());
    CHECK(added.index == 0);

    // Beats 1 to 3 are the ramp's, its end included; the others and the note stay.
    CHECK(pointsOf(song.events(f.id)) == std::vector<std::pair<Tick, int>>{{0, 10},
                                                                           {q, 100},
                                                                           {2 * q, 50},
                                                                           {3 * q, 0},
                                                                           {4 * q, 14},
                                                                           {5 * q, 15},
                                                                           {6 * q, 16},
                                                                           {7 * q, 17}});
    CHECK(song.sourceEvents(f.id) == before);
    REQUIRE(song.ramps(f.id).size() == 1);
    CHECK(song.ramps(f.id)[0] == r);

    SECTION("moving it away and back changes nothing")
    {
        auto moved = r;
        moved.start = 5 * q;
        REQUIRE(song.replaceRamp(f.id, 0, moved).done());
        CHECK(pointsOf(song.events(f.id))[1] == std::pair<Tick, int>{q, 11});
        REQUIRE(song.replaceRamp(f.id, 0, r).done());
        CHECK(pointsOf(song.events(f.id))[1] == std::pair<Tick, int>{q, 100});
        CHECK(song.sourceEvents(f.id) == before);
    }
    SECTION("removing it shows what it hid")
    {
        REQUIRE(song.removeRamp(f.id, 0));
        CHECK(song.events(f.id) == before);
        CHECK(song.ramps(f.id).empty());
        CHECK_FALSE(song.removeRamp(f.id, 0));
    }
    SECTION("converting it keeps what plays, as ordinary events")
    {
        const auto playing = song.events(f.id);
        REQUIRE(song.convertRamp(f.id, 0));
        CHECK(song.ramps(f.id).empty());
        CHECK(pointsOf(song.events(f.id)) == pointsOf(playing));
        CHECK(pointsOf(song.sourceEvents(f.id)) == pointsOf(playing));
    }
    SECTION("an edit changes what's outside it, and can't change its points")
    {
        song.updateEvents(f.id,
                          [&](EventList& events)
                          {
                              for (std::size_t i = events.size(); i-- > 0;)
                              {
                                  if (std::holds_alternative<ControlChange>(events[i].data))
                                  {
                                      events.erase(i);
                                  }
                              }
                              events.insert(modulation(2 * q + 1, 99));
                          });
        // Its points come back from it, and what it hid stays hidden beneath.
        CHECK(pointsOf(song.events(f.id)) ==
              std::vector<std::pair<Tick, int>>{{q, 100}, {2 * q, 50}, {3 * q, 0}});
        CHECK(pointsOf(song.sourceEvents(f.id)) ==
              std::vector<std::pair<Tick, int>>{{q, 11}, {2 * q, 12}, {3 * q, 13}});
        REQUIRE(song.removeRamp(f.id, 0));
        CHECK(pointsOf(song.events(f.id)) ==
              std::vector<std::pair<Tick, int>>{{q, 11}, {2 * q, 12}, {3 * q, 13}});
    }
    SECTION("other controllers aren't hidden")
    {
        song.updateEvents(f.id, [&](EventList& events)
                          { events.insert({2 * q, ControlChange{11, 90}}); });
        CHECK(pointsOf(song.events(f.id), kExpression) ==
              std::vector<std::pair<Tick, int>>{{2 * q, 90}});
    }
}

TEST_CASE("ramps of one controller on a track can't touch or overlap")
{
    Fixture f;
    const auto q = f.q;
    auto& song = f.song;
    REQUIRE(song.addRamp(f.id, ramp(q, q, 0, 127)).done());

    using R = Song::RampRefusal;
    const auto refusal = [&](const ControllerRamp& r) { return song.addRamp(f.id, r).refusal; };
    CHECK(refusal(ramp(2 * q, q, 0, 127)) == R::Collides);      // starting where it ends
    CHECK(refusal(ramp(0, q, 0, 127)) == R::Collides);          // ending where it starts
    CHECK(refusal(ramp(q + 1, 1, 0, 127)) == R::Collides);      // inside
    CHECK(refusal(ramp(0, 3 * q, 0, 127)) == R::Collides);      // around
    CHECK(refusal(ramp(q, q, 0, 127, kExpression)) == R::None); // another controller
    CHECK(refusal(ramp(2 * q + 1, q, 0, 127)) == R::None);      // just after
    REQUIRE(song.ramps(f.id).size() == 3);
    CHECK(song.ramps(f.id)[2].start == 2 * q + 1);

    const auto before = song;
    CHECK(refusal(ramp(0, q, 0, 128)) == R::Invalid);
    CHECK(refusal(ramp(song.end() - q, q, 0, 127)) == R::PastEnd);
    CHECK(song.addRamp(TrackId{99}, ramp(0, 1, 0, 1)).refusal == R::NoTrack);
    CHECK(song.replaceRamp(f.id, 9, ramp(0, 1, 0, 1)).refusal == R::NoRamp);
    // Replacing one checks it against the others alone.
    const auto collided = song.replaceRamp(f.id, 2, ramp(2 * q, q, 0, 1));
    CHECK(collided.refusal == R::Collides);
    CHECK(collided.index == 0);
    CHECK(song == before);
    CHECK(song.replaceRamp(f.id, 0, ramp(0, q / 2, 5, 6)).done());
}

TEST_CASE("a ramp keeps to its section")
{
    Fixture f;
    const auto q = f.q;
    auto& song = f.song;
    const auto bar = 4 * q;
    REQUIRE(song.splitSection(2 * bar, "B"));
    using R = Song::RampRefusal;
    CHECK(song.addRamp(f.id, ramp(bar, bar, 0, 127)).refusal == R::CrossesSection);
    // Across the line between bars 1 and 2, and in bar 3.
    REQUIRE(song.addRamp(f.id, ramp(bar - q, bar, 0, 127)).done());
    REQUIRE(song.addRamp(f.id, ramp(2 * bar, q, 0, 127)).done());
    CHECK(song.sections()[0]->content(f.id)->ramps.size() == 1);
    REQUIRE(song.sections()[1]->content(f.id)->ramps.size() == 1);
    CHECK(song.sections()[1]->content(f.id)->ramps[0].start == 0);

    SECTION("splitting, inserting or removing bars through one is refused")
    {
        const auto before = song;
        CHECK_FALSE(song.splitSection(bar, "C"));
        CHECK_FALSE(song.splitSection(2 * bar - q, "C")); // at its end
        auto removed = song.removeBars(bar, 1);
        CHECK_FALSE(removed.done);
        CHECK(removed.ramp == f.id);
        removed = song.removeBars(0, 1);
        CHECK_FALSE(removed.done);
        CHECK(removed.ramp == f.id);
        const auto inserted = song.insertBars(bar, 1);
        CHECK_FALSE(inserted.done);
        CHECK(inserted.ramp == f.id);
        CHECK(song == before);
    }
    SECTION("a split at its start takes it along")
    {
        REQUIRE(song.splitSection(bar - q, "A2"));
        CHECK(song.sections()[1]->content(f.id)->ramps[0].start == 0);
        CHECK(song.ramps(f.id)[0].start == bar - q);
    }
    SECTION("removing the bars it's in removes it, and inserting bars before it moves it")
    {
        REQUIRE(song.removeBars(2 * bar, 1).done);
        REQUIRE(song.ramps(f.id).size() == 1);
        REQUIRE(song.insertBars(0, 1).done);
        CHECK(song.ramps(f.id)[0].start == 2 * bar - q);
    }
    SECTION("joining sections keeps both")
    {
        REQUIRE(song.joinSections(0));
        REQUIRE(song.sections()[0]->content(f.id)->ramps.size() == 2);
        CHECK(song.sections()[0]->content(f.id)->ramps[1].start == 2 * bar);
    }
    SECTION("shortening a section through one is refused")
    {
        REQUIRE(song.addRamp(f.id, ramp(song.end() - bar, q, 0, 1)).done());
        REQUIRE(song.resizeSection(1, -1));
        CHECK(song.ramps(f.id).size() == 2);
        const auto before = song;
        CHECK_FALSE(song.resizeSection(0, -1));
        CHECK(song == before);
    }
    SECTION("a copied track brings its ramps and what they hide")
    {
        const auto copy = song.copyTrack(f.id);
        REQUIRE(copy);
        const auto pasted = song.insertTrack(1, *copy);
        REQUIRE(pasted);
        CHECK(std::ranges::equal(song.ramps(*pasted), song.ramps(f.id)));
        CHECK(song.sourceEvents(*pasted) == song.sourceEvents(f.id));
        CHECK(song.events(*pasted) == song.events(f.id));
    }
}

TEST_CASE("a song file keeps ramps and the events they hide")
{
    Fixture f;
    const auto q = f.q;
    auto& song = f.song;
    auto curved = ramp(q, 2 * q, 100, 0);
    curved.shape = ControllerRamp::Shape::Curve;
    curved.amount = -40;
    curved.density = 32;
    REQUIRE(song.addRamp(f.id, curved).done());
    REQUIRE(
        song.addRamp(f.id, ramp(4 * q, q, -8192, 0, Lane{.kind = Lane::Kind::PitchBend})).done());
    const auto text = writeSong(song);
    CHECK(text.find("ramp 1:2:0 cc1 len=1/2 from=100 to=0 shape=curve amount=-40 density=32\n") !=
          std::string::npos);
    CHECK(text.find("ramp 2:1:0 bend len=1/4 from=-8192 to=0\n") != std::string::npos);

    const auto read = readSong(text, f.ppqn);
    REQUIRE(read.error.empty());
    REQUIRE(read.song);
    CHECK(*read.song == song);
    CHECK(read.song->events(f.id) == song.events(f.id));
    CHECK(read.song->sourceEvents(f.id) == song.sourceEvents(f.id));

    SECTION("at another resolution")
    {
        const Ppqn other{f.ppqn == kPpqn ? 96 : kPpqn.value};
        const auto rescaled = readSong(text, other);
        REQUIRE(rescaled.error.empty());
        REQUIRE(rescaled.song);
        REQUIRE(rescaled.song->ramps(f.id).size() == 2);
        CHECK(rescaled.song->ramps(f.id)[0].start == other.quarter());
        CHECK(rescaled.song->ramps(f.id)[0].length == 2 * other.quarter());
    }
}

TEST_CASE("a song file's ramps are checked")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const std::string head = "modal-song 1\nsong \"\" ppqn=" + std::to_string(ppqn.value) +
                             "\ntrack 1 \"A\" ch=1\nsection \"S\" bars=1\n  meter 1:1:0 4/4\n"
                             "  tempo 1:1:0 120\n  track 1\n";
    const auto error = [&](const std::string& lines) { return readSong(head + lines, ppqn).error; };
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=127\n").empty());
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=127 shape=scurve density=96\n").empty());
    CHECK(error("    ramp 1:1:0 cc64 len=1/4 from=0 to=127\n").find("continuous") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0\n").find("needs its length and values") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=128\n").find("from 0 to 127") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=0 from=0 to=1\n").find("needs a length") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/1 from=0 to=1\n").find("past the end") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=1 shape=wobbly\n").find("linear, curve") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=1 colour=red\n").find("unknown ramp") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=1\n    ramp 1:2:0 cc1 len=1/4 from=0 to=1\n")
              .find("touches or overlaps") != std::string::npos);
    CHECK(error("    1:1:0 cc 1 5\n    meter 1:1:0 4/4\n").find("meter lines come first") !=
          std::string::npos);
    CHECK(error("    ramp 1:1:0 cc1 len=1/4 from=0 to=1\n    meter 1:1:0 4/4\n")
              .find("meter lines come first") != std::string::npos);
}

TEST_CASE("a MIDI file is written with what plays")
{
    const auto q = kPpqn.quarter();
    Song song{kPpqn};
    const auto id = song.addTrack("Strings");
    song.updateEvents(id, [&](EventList& events) { events.insert(modulation(q, 77)); });
    auto r = ramp(0, 2 * q, 0, 100);
    r.density = 1;
    REQUIRE(song.addRamp(id, r).done());
    const auto exported = exportSmf(song);
    REQUIRE(exported.error.empty());
    const auto imported = importSmf(exported.bytes);
    REQUIRE(imported.song);
    REQUIRE(imported.song->tracks().size() == 1);
    CHECK(pointsOf(imported.song->events(imported.song->tracks()[0]->id())) ==
          std::vector<std::pair<Tick, int>>{{0, 0}, {q, 50}, {2 * q, 100}});
    CHECK(imported.song->ramps(imported.song->tracks()[0]->id()).empty());
}
