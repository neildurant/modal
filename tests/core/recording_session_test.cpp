// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/ppqn.hpp"

#include <modal/core/recording_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;

namespace
{

Song recordingSong(Ppqn ppqn)
{
    Song song{ppqn};
    const auto id = song.addTrack("Lead");
    song.updateTrack(id, [](Track& track) { track.arm = true; });
    song.addTrack("Other");
    song.countIn.mode = CountInSettings::Mode::None;
    song.metronome.when = MetronomeSettings::When::Off;
    return song;
}

struct Recording
{
    explicit Recording(Ppqn ppqn)
        : document(recordingSong(ppqn))
    {
        document.setListener([&](const Song& song) { player.update(song); });
        player.update(document.underlying());
    }

    TrackId lead() const { return document.underlying().tracks()[0]->id(); }
    TrackId other() const { return document.underlying().tracks()[1]->id(); }
    void step() { engine.step(); }
    void advance(std::chrono::nanoseconds by)
    {
        clock.time += by;
        step();
        player.poll();
        player.takeInput();
        session.refreshPreview();
    }
    void receive(std::array<std::uint8_t, 3> bytes)
    {
        REQUIRE(engine.receive(InputId{0}, bytes, clock.time));
    }

    RecordingSink sink;
    FakeClock clock;
    Engine engine{sink, clock};
    Player player{engine, [](std::string_view) { return DestinationId{1}; }};
    Document document;
    RecordingSession session{document};
    std::chrono::nanoseconds started{};
};

} // namespace

TEST_CASE("a recording session keeps its take pending until one acknowledged undo commit")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    Recording r{ppqn};
    REQUIRE(r.session.state() == RecordingSession::State::Idle);
    CHECK_FALSE(r.session.fileOperationRefusal());
    CHECK_FALSE(r.session.eventEditRefusal(r.lead()));
    const auto before = r.document.underlying();
    REQUIRE(r.session.start(r.player));
    CHECK(r.session.state() == RecordingSession::State::Recording);
    CHECK(r.document.hasPreview());
    CHECK(r.session.fileOperationRefusal() == "Stop recording first");
    REQUIRE(r.session.tracks().size() == 1);
    CHECK(r.session.tracks()[0] == r.lead());
    r.step();
    r.advance(250ms);
    r.receive({0x90, 60, 100});
    r.advance(500ms);
    REQUIRE(r.document.song().events(r.lead()).size() == 1);
    CHECK(r.document.underlying() == before);
    CHECK_FALSE(r.document.modified());
    REQUIRE(r.session.stop());
    CHECK(r.session.state() == RecordingSession::State::Finishing);
    CHECK(r.session.fileOperationRefusal() == "Finishing the take: try again in a moment");
    CHECK_FALSE(r.session.finish({}, "Record Lead"));
    CHECK_FALSE(r.session.start(r.player));
    CHECK_FALSE(r.document.edit("Still recorded", {},
                                [&](Song& song)
                                {
                                    song.updateEvents(
                                        r.lead(), [](EventList& events)
                                        { events.insert({0, ControlChange{1, 20}}); });
                                }));
    CHECK(r.document.takeRefusal() == "Lead is being recorded: change it once recording stops");
    // This final release must be collected before the take is committed.
    r.receive({0x80, 60, 70});
    r.step();
    bool pendingDuringCommit = false;
    r.document.setListener(
        [&](const Song& song)
        {
            pendingDuringCommit = r.session.pending() && !r.document.hasPreview() &&
                                  r.session.fileOperationRefusal().has_value();
            r.player.update(song);
        });
    const EditPlace place{.track = r.lead(), .tick = q};
    const auto completed = r.session.finish(place, "Record Lead");
    REQUIRE(completed);
    CHECK(completed->changed);
    CHECK(completed->tracks == std::vector<TrackId>{r.lead()});
    CHECK(completed->inputLost == 0);
    CHECK(pendingDuringCommit);
    CHECK(r.session.state() == RecordingSession::State::Idle);
    CHECK_FALSE(r.document.hasPreview());
    CHECK_FALSE(r.session.fileOperationRefusal());
    CHECK(r.session.tracks().empty());
    REQUIRE(r.document.underlying().events(r.lead()).size() == 1);
    CHECK(r.document.underlying().events(r.lead())[0] == Event{q / 2, Note{60, 100, q, 70}, 1});
    const auto version = r.document.version();
    CHECK_FALSE(r.session.finish({}, "Again"));
    CHECK(r.document.version() == version);
    const auto undone = r.document.undo();
    REQUIRE(undone);
    CHECK(undone->title == "Record Lead");
    CHECK(undone->place == place);
    CHECK(r.document.underlying() == before);
    CHECK_FALSE(r.document.canUndo());
    REQUIRE(r.document.redo());
    CHECK(r.document.underlying().events(r.lead()).size() == 1);
}

TEST_CASE("a recording session refuses a failed start without installing document hooks")
{
    Recording r{MODAL_ANY_PPQN()};
    const auto before = r.document.underlying();
    const bool fullQueue = GENERATE(false, true);
    if (fullQueue)
    {
        while (r.engine.stop())
        {
        }
    }
    else
    {
        REQUIRE(r.player.play(r.document.underlying()));
    }
    CHECK_FALSE(r.session.start(r.player));
    CHECK_FALSE(r.session.pending());
    CHECK_FALSE(r.document.hasPreview());
    CHECK_FALSE(r.session.fileOperationRefusal());
    CHECK(r.document.underlying() == before);
    CHECK_FALSE(r.document.canUndo());
    CHECK(r.document.edit("Still editable", {},
                          [&](Song& song)
                          {
                              song.updateEvents(r.lead(), [](EventList& events)
                                                { events.insert({0, ControlChange{1, 30}}); });
                          }));
}

TEST_CASE("a recording session guards recorded events and undo while permitting other edits")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    Recording r{ppqn};
    REQUIRE(r.document.edit("Before recording", {},
                            [&](Song& song)
                            {
                                song.updateEvents(r.lead(), [&](EventList& events)
                                                  { events.insert({q, Note{60, 100, q}}); });
                            }));
    REQUIRE(r.session.start(r.player));
    CHECK(r.session.eventEditRefusal(r.lead()) ==
          "Lead is being recorded: change it once recording stops");
    CHECK_FALSE(r.session.eventEditRefusal(r.other()));
    const auto before = r.document.underlying();
    const auto version = r.document.version();
    CHECK_FALSE(r.document.undo());
    CHECK(r.document.takeRefusal() == "Lead is being recorded: change it once recording stops");
    CHECK_FALSE(r.document.edit("Refused", {},
                                [&](Song& song)
                                {
                                    song.updateEvents(r.lead(), [&](EventList& events)
                                                      { events.insert({2 * q, Note{62, 90, q}}); });
                                }));
    CHECK(r.document.underlying() == before);
    CHECK(r.document.version() == version);
    REQUIRE(r.document.edit("Other track", {},
                            [&](Song& song)
                            {
                                song.updateEvents(r.other(), [&](EventList& events)
                                                  { events.insert({0, Note{40, 90, q}}); });
                            }));
    REQUIRE(r.document.edit(
        "Track setting", {}, [&](Song& song)
        { song.updateTrack(r.lead(), [](Track& track) { track.name = "Keys"; }); }));
    CHECK_FALSE(r.document.edit("Refused again", {},
                                [&](Song& song)
                                {
                                    song.updateEvents(r.lead(), [&](EventList& events)
                                                      { events.insert({2 * q, Note{62, 90, q}}); });
                                }));
    CHECK(r.document.takeRefusal() == "Keys is being recorded: change it once recording stops");
    CHECK(r.session.eventEditRefusal(r.lead()) ==
          "Keys is being recorded: change it once recording stops");
}

TEST_CASE("stopping a recording session in the count-in releases its hooks without an undo step")
{
    Recording r{MODAL_ANY_PPQN()};
    auto song = r.document.underlying();
    song.countIn.mode = CountInSettings::Mode::CountIn;
    song.countIn.bars = 1;
    r.document.replace(std::move(song), std::nullopt);
    const auto before = r.document.underlying();
    REQUIRE(r.session.start(r.player));
    r.advance(500ms);
    CHECK(r.player.recordPhase() == Player::RecordPhase::CountIn);
    CHECK_FALSE(r.session.refreshPreview());
    REQUIRE(r.session.stop());
    CHECK_FALSE(r.session.finish({}, "Stopped early"));
    r.step();
    const auto completed = r.session.finish({}, "Stopped early");
    REQUIRE(completed);
    CHECK_FALSE(completed->changed);
    CHECK(r.document.underlying() == before);
    CHECK_FALSE(r.document.modified());
    CHECK_FALSE(r.document.canUndo());
    CHECK_FALSE(r.document.hasPreview());
    CHECK_FALSE(r.session.pending());
}

TEST_CASE(
    "panic completes a recording session once and consecutive takes remain separate undo steps")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Recording r{ppqn};
    for (const auto pitch : {60, 62})
    {
        REQUIRE(r.session.start(r.player));
        r.step();
        r.receive({0x90, static_cast<std::uint8_t>(pitch), 100});
        r.advance(500ms);
        REQUIRE(r.player.panic());
        CHECK(r.session.pending());
        CHECK_FALSE(r.session.finish({}, "Take"));
        r.step();
        const auto completed = r.session.finish({}, "Take");
        REQUIRE(completed);
        CHECK(completed->changed);
        CHECK_FALSE(r.session.finish({}, "Duplicate"));
        REQUIRE(r.document.underlying().events(r.lead()).size() == 1);
        CHECK(std::get<Note>(r.document.underlying().events(r.lead())[0].data).pitch == pitch);
    }
    REQUIRE(r.document.undo());
    CHECK(std::get<Note>(r.document.underlying().events(r.lead())[0].data).pitch == 60);
    REQUIRE(r.document.undo());
    CHECK(r.document.underlying().events(r.lead()).empty());
    CHECK_FALSE(r.document.canUndo());
}

TEST_CASE("recording session input-loss accounting starts afresh with each take")
{
    Recording r{MODAL_ANY_PPQN()};
    REQUIRE(r.session.start(r.player));
    r.step();
    std::vector<std::uint8_t> tooLarge(InputQueue::kMaxSysEx + 1, 0);
    tooLarge.front() = 0xF0;
    tooLarge.back() = 0xF7;
    CHECK_FALSE(r.engine.receive(InputId{0}, tooLarge, r.clock.time));
    CHECK(r.session.inputLost());
    r.advance(500ms);
    REQUIRE(r.session.stop());
    r.step();
    const auto completed = r.session.finish({}, "Empty");
    REQUIRE(completed);
    CHECK(completed->inputLost == 1);
    CHECK_FALSE(r.session.inputLost());
    REQUIRE(r.session.start(r.player));
    CHECK_FALSE(r.session.inputLost());
}

TEST_CASE("recording session destruction removes its document callbacks")
{
    Recording r{MODAL_ANY_PPQN()};
    {
        RecordingSession session{r.document};
        REQUIRE(session.start(r.player));
        r.step();
        r.receive({0x90, 60, 100});
        r.advance(500ms);
        CHECK(r.document.hasPreview());
    }
    CHECK_FALSE(r.document.hasPreview());
    r.document.refreshPreview();
    REQUIRE(r.document.edit("After session destruction", {},
                            [&](Song& song)
                            {
                                song.updateEvents(r.lead(), [](EventList& events)
                                                  { events.insert({0, ControlChange{1, 40}}); });
                            }));
}

TEST_CASE("an idle recording session does not treat an unrelated preview as a pending take")
{
    Recording r{MODAL_ANY_PPQN()};
    r.document.setPreview([](const Song& song) { return song; });
    CHECK(r.document.hasPreview());
    CHECK_FALSE(r.session.pending());
    CHECK_FALSE(r.session.fileOperationRefusal());
    {
        RecordingSession idle{r.document};
    }
    CHECK(r.document.hasPreview());
}

TEST_CASE("a failed recording-session start preserves hooks it does not own")
{
    Recording r{MODAL_ANY_PPQN()};
    r.document.setPreview([](const Song& song) { return song; });
    r.document.setGuard([](const Song&, const Song&) -> std::optional<std::string>
                        { return "Existing guard"; });
    REQUIRE(r.player.play(r.document.underlying()));
    CHECK_FALSE(r.session.start(r.player));
    CHECK(r.document.hasPreview());
    CHECK_FALSE(r.document.edit("Existing guard still applies", {},
                                [](Song& song) { song.title = "Changed"; }));
    CHECK(r.document.takeRefusal() == "Existing guard");
}

TEST_CASE("recording-session completion preserves other edits and gives the take its own undo step")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    Recording r{ppqn};
    REQUIRE(r.session.start(r.player));
    r.step();
    r.receive({0x90, 60, 100});
    r.advance(500ms);
    REQUIRE(r.document.edit("Other track", {},
                            [&](Song& song)
                            {
                                song.updateEvents(r.other(), [&](EventList& events)
                                                  { events.insert({q, Note{40, 90, q}}); });
                            }));
    REQUIRE(r.session.stop());
    r.step();
    const auto completed = r.session.finish({}, "Recorded take");
    REQUIRE(completed);
    CHECK(completed->changed);
    CHECK(r.document.underlying().events(r.lead()).size() == 1);
    CHECK(r.document.underlying().events(r.other()).size() == 1);
    const auto undone = r.document.undo();
    REQUIRE(undone);
    CHECK(undone->title == "Recorded take");
    CHECK(r.document.underlying().events(r.lead()).empty());
    CHECK(r.document.underlying().events(r.other()).size() == 1);
    REQUIRE(r.document.undo());
    CHECK(r.document.underlying().events(r.other()).empty());
    CHECK_FALSE(r.document.canUndo());
}

namespace
{

// Runs the engine on to a time after recording started, a little at a time, as the window's
// frames would.
void runTo(Recording& r, std::chrono::nanoseconds after)
{
    const auto time = r.started + after;
    while (r.clock.time + 20ms <= time)
        r.advance(20ms);
    if (r.clock.time < time)
        r.advance(time - r.clock.time);
}

Tick noteEnd(const Event& event)
{
    return event.tick + std::get<Note>(event.data).duration;
}

const Event* eventAt(const EventList& events, Tick tick, bool note)
{
    for (const auto& event : events.all())
        if (event.tick == tick && event.isNote() == note)
            return &event;
    return nullptr;
}

} // namespace

TEST_CASE("a punch records only its range, ending there as playback carries on")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto bar = ppqn.whole();
    Recording r{ppqn};
    // A bar's pre-roll before punching in at bar 2, out at bar 3. The old part: a note running
    // into the range, one inside it, one after it, and the sustain pedal down throughout.
    r.document.edit("Set up", {},
                    [&](Song& song)
                    {
                        song.countIn.mode = CountInSettings::Mode::PreRoll;
                        song.updateEvents(r.lead(),
                                          [&](EventList& events)
                                          {
                                              events.insert({0, ControlChange{64, 127}});
                                              events.insert({0, Note{40, 90, 2 * bar}});
                                              events.insert({bar + q, Note{41, 90, q}});
                                              events.insert({2 * bar + q, Note{42, 90, q}});
                                              events.insert({3 * bar, ControlChange{64, 0}});
                                          });
                    });
    REQUIRE(r.session.start(r.player, ArmedSpan{bar, 2 * bar}));
    r.started = r.clock.time;
    r.step();
    CHECK(r.player.recordPhase() == Player::RecordPhase::PreRoll);

    runTo(r, 1000ms);           // the pre-roll's third beat
    r.receive({0x90, 50, 100}); // held into the range: starts at punch-in
    runTo(r, 1500ms);
    r.receive({0x90, 51, 100}); // played and let go in the pre-roll: dropped
    runTo(r, 1600ms);
    r.receive({0x80, 51, 0});
    runTo(r, 2250ms);
    r.receive({0x80, 50, 0});
    CHECK(r.player.recordPhase() == Player::RecordPhase::Recording);
    runTo(r, 3000ms);
    r.receive({0x90, 60, 100}); // held past punch-out: ends there
    runTo(r, 3900ms);
    CHECK(r.session.state() == RecordingSession::State::Recording);

    // Past punch-out: recording ends, playback carries on, and the take goes in as one step.
    runTo(r, 4100ms);
    CHECK(r.player.punchedOut());
    CHECK_FALSE(r.player.recording());
    CHECK(r.session.state() == RecordingSession::State::Finishing);
    const auto steps = r.document.version();
    const auto completed = r.session.finish({}, "Record Lead");
    REQUIRE(completed);
    CHECK(completed->changed);
    CHECK(r.document.version() != steps);
    CHECK(r.player.playing());
    CHECK(r.session.state() == RecordingSession::State::Idle);

    const auto& events = r.document.underlying().events(r.lead());
    const auto* cut = eventAt(events, 0, true);
    REQUIRE(cut != nullptr);
    CHECK(noteEnd(*cut) == bar); // the old note cut at punch-in
    const auto* held = eventAt(events, bar, true);
    REQUIRE(held != nullptr);
    CHECK(std::get<Note>(held->data).pitch == 50);
    CHECK(noteEnd(*held) == bar + q / 2);
    CHECK_FALSE(eventAt(events, bar + q, true)); // the old note inside it gone
    const auto* last = eventAt(events, bar + 2 * q, true);
    REQUIRE(last != nullptr);
    CHECK(std::get<Note>(last->data).pitch == 60);
    CHECK(noteEnd(*last) == 2 * bar);
    CHECK(eventAt(events, 2 * bar + q, true)); // after it, kept
    // The pedal let up at punch-in and put down again at punch-out, as it was there.
    const auto* up = eventAt(events, bar, false);
    REQUIRE(up != nullptr);
    CHECK(up->data == EventData{ControlChange{64, 0}});
    const auto* down = eventAt(events, 2 * bar, false);
    REQUIRE(down != nullptr);
    CHECK(down->data == EventData{ControlChange{64, 127}});
    const auto notes = std::ranges::count_if(events.all(), &Event::isNote);
    CHECK(notes == 4);

    // Played after punch-out: nothing more recorded.
    r.receive({0x90, 70, 100});
    runTo(r, 4500ms);
    CHECK(std::ranges::count_if(r.document.underlying().events(r.lead()).all(), &Event::isNote) ==
          4);
    REQUIRE(r.player.stop());
    REQUIRE(r.document.undo());
    CHECK(eventAt(r.document.underlying().events(r.lead()), bar + q, true));
}

TEST_CASE("a punch stopped early records to the stop, and overdubbing keeps what's there")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto bar = ppqn.whole();
    const bool overdub = GENERATE(false, true);
    Recording r{ppqn};
    r.document.edit("Set up", {},
                    [&](Song& song)
                    {
                        song.recordMode = overdub ? RecordMode::Overdub : RecordMode::Replace;
                        song.updateEvents(r.lead(), [&](EventList& events)
                                          { events.insert({bar + 3 * q, Note{41, 90, q}}); });
                    });
    // No lead-in: playing starts at punch-in.
    REQUIRE(r.session.start(r.player, ArmedSpan{bar, 3 * bar}));
    r.started = r.clock.time;
    r.step();
    CHECK(r.player.recordPhase() == Player::RecordPhase::Recording);
    runTo(r, 500ms);
    r.receive({0x90, 60, 100});
    runTo(r, 1000ms);
    REQUIRE(r.session.stop());
    r.step();
    runTo(r, 1100ms);
    const auto completed = r.session.finish({}, "Record Lead");
    REQUIRE(completed);
    CHECK_FALSE(r.player.playing());
    const auto& events = r.document.underlying().events(r.lead());
    const auto* note = eventAt(events, bar + q, true);
    REQUIRE(note != nullptr);
    CHECK(noteEnd(*note) == bar + 2 * q); // held to the stop
    // Past where it stopped, the old note stays, whichever the mode.
    CHECK(eventAt(events, bar + 3 * q, true));
}

TEST_CASE("punching in on the fly records from punch-in without a lead-in, and only before it")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const auto bar = ppqn.whole();
    Recording r{ppqn};
    r.document.edit("Set up", {},
                    [&](Song& song)
                    {
                        song.updateEvents(r.lead(), [&](EventList& events)
                                          { events.insert({bar + q, Note{41, 90, q}}); });
                    });
    REQUIRE(r.player.play(r.document.underlying()));
    r.started = r.clock.time;
    r.step();
    runTo(r, 500ms);
    // A punch the playhead has passed can't start; one ahead of it can.
    CHECK_FALSE(r.session.start(r.player, ArmedSpan{0, bar}));
    CHECK_FALSE(r.session.start(r.player)); // without a punch, nothing to start while playing
    REQUIRE(r.session.start(r.player, ArmedSpan{bar, 2 * bar}));
    CHECK(r.player.playing());
    CHECK(r.player.recordPhase() == Player::RecordPhase::PreRoll);
    runTo(r, 1500ms);
    r.receive({0x90, 50, 100}); // held into the range: starts at punch-in
    runTo(r, 2500ms);
    CHECK(r.player.recordPhase() == Player::RecordPhase::Recording);
    r.receive({0x80, 50, 0});
    runTo(r, 4100ms);
    CHECK(r.player.punchedOut());
    const auto completed = r.session.finish({}, "Record Lead");
    REQUIRE(completed);
    CHECK(r.player.playing());
    const auto& events = r.document.underlying().events(r.lead());
    const auto* note = eventAt(events, bar, true);
    REQUIRE(note != nullptr);
    CHECK(noteEnd(*note) == bar + q);
    CHECK_FALSE(eventAt(events, bar + q, true)); // replaced
}

TEST_CASE("a playback loop can't be punched into on the fly")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto bar = ppqn.whole();
    Recording r{ppqn};
    REQUIRE(r.player.playLoop(r.document.underlying(), 0, 4 * bar));
    r.step();
    CHECK_FALSE(r.session.start(r.player, ArmedSpan{2 * bar, 3 * bar}));
    CHECK(r.session.state() == RecordingSession::State::Idle);
}
