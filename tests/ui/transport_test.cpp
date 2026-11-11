// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace modal;
using namespace modal::ui;
using namespace std::chrono_literals;

namespace
{

constexpr CellSize kCell{10, 20};

// The keys from the Tracks view's Name column, where it opens, to a field's column.
std::u32string toColumn(TrackField field)
{
    const auto columns = trackColumns();
    const auto name = std::ranges::find(columns, TrackField::Name, &TrackColumn::field);
    const auto to = std::ranges::find(columns, field, &TrackColumn::field);
    std::u32string keys;
    for (const char c : std::to_string(to - name))
    {
        keys += static_cast<char32_t>(c);
    }
    return keys + U'l';
}

// The demo song's event list, playing through an engine on a clock that stands still until
// moved, every port going to one destination.
struct Playing
{
    Playing()
    {
        registry.add(windowCommands());
        ws.resize(80 * kCell.width, 24 * kCell.height, kCell);
        player.update(document.song());
        ws.setPlayer(&player);
    }

    void press(const Key& key) { ws.handle(key, now); }
    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            press(Key::typed(key));
        }
    }

    // Lets the engine play for a while, then wakes the view at its deadline, as the window
    // would to draw a frame.
    void runFor(std::chrono::nanoseconds time)
    {
        test::runUntil(engine, clock, clock.time + time);
        now = ws.deadline().value_or(now);
        ws.wake(now);
    }

    std::u32string status() const
    {
        std::u32string text;
        const auto frame = ws.frame();
        for (const auto& run : frame.back().text)
        {
            text += run.text;
        }
        return text;
    }

    core::Tick tickOf(std::size_t row) const { return view.list().rows()[row].tick; }

    // The row the marker shows on, as a run in the list's margin, if it's in view.
    std::optional<std::size_t> markerRow() const
    {
        const auto frame = ws.frame();
        for (const auto& run : frame.front().text)
        {
            if (run.x == 0 && run.text.starts_with(U"▶"))
            {
                // The first line is the headings.
                return view.top() + static_cast<std::size_t>(run.y / kCell.height) - 1;
            }
        }
        return std::nullopt;
    }

    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    core::Player player{engine, [](std::string_view) { return core::DestinationId{1}; }};
    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
    EventListView& view = ws.eventList();
    std::int64_t now = 100;
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("Space plays from the marker and stops where it is; with rewind, the marker stays")
{
    Playing p;
    REQUIRE(p.ws.rewind());
    p.type(U"5j");
    CHECK(p.ws.deadline() == std::nullopt);
    CHECK(contains(p.status(), U"Rewind on"));

    CHECK(contains(p.status(), U"■"));
    CHECK(contains(p.status(), U"1:1:000"));
    CHECK(p.markerRow() == 3); // the last of the rows at the start: section, meter, tempo
    p.type(U" ");
    CHECK(p.player.playing());
    CHECK(p.view.playheadRow() == 3); // the last of the rows at the start: section, meter, tempo
    CHECK(p.ws.deadline() == p.now + Workspace::kFrameInterval);

    p.runFor(1s);
    REQUIRE(p.view.playheadRow());
    const auto early = *p.view.playheadRow();
    CHECK(early > 3);
    CHECK(p.view.row() == 5); // the cursor stays
    CHECK(contains(p.status(), U"▶"));

    // Once the playhead is a third of the way down, the list scrolls under it.
    const auto third = static_cast<std::size_t>(p.view.viewRows()) / 3;
    p.runFor(10s);
    const auto row = *p.view.playheadRow();
    CHECK(row > early);
    CHECK(row == p.view.top() + third);
    CHECK(p.view.row() == 5);

    // Stopped where it is, the cursor landing there, and the marker where it was.
    p.type(U" ");
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.row() == row);
    CHECK(p.view.top() > 5);
    CHECK_FALSE(contains(p.status(), U"▶"));
    p.runFor(1ms);
    CHECK(p.ws.deadline() == std::nullopt); // no more frames
    CHECK(p.player.marker() == 0);
    p.type(U"gg");
    CHECK(p.markerRow() == 3);
}

TEST_CASE("without rewind, the cursor follows the playhead, and the marker moves where it stops")
{
    Playing p;
    p.ws.setRewind(false);
    CHECK(contains(p.status(), U"Rewind off"));
    p.type(U" ");
    p.runFor(10s);
    const auto row = *p.view.playheadRow();
    CHECK(p.view.row() == row);
    CHECK(row == p.view.top() + static_cast<std::size_t>(p.view.viewRows()) / 3); // as with rewind

    p.type(U" ");
    p.runFor(1ms); // the engine says where it stopped
    CHECK_FALSE(p.player.stopping());
    CHECK(p.ws.deadline() == std::nullopt);
    CHECK(p.view.row() == row);
    CHECK(p.player.marker() >= p.tickOf(row));
    CHECK(p.markerRow() == row);
}

TEST_CASE("m Space puts the marker at the cursor, and Ctrl-Space plays from there too")
{
    Playing p;
    p.type(U"9jm ");
    CHECK_FALSE(p.player.playing());
    CHECK(p.player.marker() == p.tickOf(9));
    CHECK(p.markerRow() == 9);
    p.type(U"j");
    p.press(Key::typed(U' ', kCtrl));
    CHECK(p.player.playing());
    CHECK(p.player.marker() == p.tickOf(10));
    CHECK(p.view.playheadRow() == 10);
}

TEST_CASE("moving the cursor while playing lets go of the playhead, until zf or playing again")
{
    Playing p;
    p.type(U" ");
    p.runFor(11s);
    REQUIRE(p.view.top() > 0); // scrolled with the playhead
    p.type(U"gg");
    CHECK(p.view.top() == 0);
    p.runFor(2s);
    CHECK(p.view.top() == 0); // staying with the cursor
    p.type(U"zf");
    CHECK(p.view.top() > 0);

    // Stopping goes back to the cursor, even without rewind, once moved away.
    p.type(U"gg");
    p.ws.setRewind(false);
    p.type(U" ");
    p.runFor(1ms);
    CHECK(p.view.row() == 0);

    // Playing again follows again.
    p.type(U" ");
    p.runFor(11s);
    CHECK(p.view.top() > 0);
}

TEST_CASE("the piano roll shows the play-from marker in its ruler")
{
    Playing p;
    p.type(U"gr}m ");
    bool shown = false;
    const auto frame = p.ws.frame();
    for (const auto& region : frame)
    {
        for (const auto& run : region.text)
        {
            shown = shown || (run.text == U"\u25B6" && region.bounds.y > 0);
        }
    }
    CHECK(shown);
}

TEST_CASE("near the list's end, the playhead goes on down the view")
{
    Playing p;
    p.type(U"G5k");
    const auto end = p.view.row() + 5;
    p.press(Key::typed(U' ', kCtrl));
    p.runFor(1ms);
    const auto rows = static_cast<std::size_t>(p.view.viewRows());
    CHECK(p.view.top() == end + 1 - rows); // the last row at the bottom
    CHECK(*p.view.playheadRow() > p.view.top() + rows / 3);
}

TEST_CASE("playback stopping by itself at the song's end goes back to the cursor")
{
    Playing p;
    p.type(U"G");
    const auto end = p.view.row();
    p.type(U"k");
    p.press(Key::typed(U' ', kCtrl));
    p.runFor(30s);
    CHECK_FALSE(p.player.playing());
    CHECK(p.view.playheadRow() == std::nullopt);
    CHECK(p.view.row() == end - 1);
}

TEST_CASE("the leader then t and Space turns rewind on and off")
{
    Playing p;
    p.type(U",T ");
    CHECK_FALSE(p.ws.rewind());
    CHECK(p.ws.message() == "Rewind off: the play-from marker moves to where playback stops");
    CHECK_FALSE(p.player.rewind());
    CHECK_FALSE(p.player.playing());
    p.type(U",T ");
    CHECK(p.ws.rewind());
}

TEST_CASE("Ctrl-. panics, at the prompt too")
{
    Playing p;
    p.type(U" ");
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());

    p.type(U" :");
    REQUIRE(p.ws.mode() == Mode::CommandLine);
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());
    CHECK(p.ws.mode() == Mode::CommandLine);
    CHECK(p.ws.commandLine().text().empty()); // not typed
}

TEST_CASE("without a player, the transport says there's nothing to play to")
{
    Playing p;
    p.ws.setPlayer(nullptr);
    p.type(U" ");
    CHECK(p.ws.message() == "Nothing to play to");
}

TEST_CASE("Shift-Space stops and goes back to where the cursor was when playback started")
{
    Playing p;
    p.type(U"5j ");
    p.runFor(11s);
    REQUIRE(p.view.top() > 5);
    p.press(Key::typed(U' ', kShift));
    CHECK_FALSE(p.player.playing());
    p.runFor(1ms);
    CHECK(p.view.row() == 5);
    CHECK(p.view.top() <= 5);
    CHECK(p.player.marker() == 0); // with rewind, where it was

    // Without rewind too, the marker moving to where it stopped.
    p.ws.setRewind(false);
    p.type(U" ");
    p.runFor(11s);
    p.press(Key::typed(U' ', kShift));
    p.runFor(1ms);
    CHECK(p.view.row() == 5);
    CHECK(p.player.marker() > 0);

    // Stopped, it plays, as Space does.
    p.press(Key::typed(U' ', kShift));
    CHECK(p.player.playing());
}

TEST_CASE("detaching a stopping player leaves views safe to switch and wake")
{
    Playing p;
    p.type(U" ");
    p.runFor(1s);
    REQUIRE(p.player.stop());
    p.ws.setPlayer(nullptr);
    p.type(U"gtgrgega");
    p.ws.wake(p.now + 20);
    CHECK(p.ws.shown() == ViewKind::Arrangement);
    CHECK_FALSE(p.ws.frame().empty());
}

TEST_CASE("a setup edited in playback says when a track's own program change holds it back")
{
    Playing p;
    const auto id = p.document.song().tracks()[0]->id();
    const auto q = p.document.song().ppqn().quarter();
    p.document.edit("Add a program", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(id, [&](core::EventList& events)
                                          { events.insert({q / 2, core::ProgramChange{3}}); });
                    });
    p.type(U"gt" + toColumn(TrackField::Program));
    p.press(Key::typed(U' '));
    p.runFor(100ms);

    const auto set = [&](std::u32string_view value)
    {
        p.press(Key::of(NamedKey::Enter));
        p.type(value);
        p.press(Key::of(NamedKey::Enter));
    };
    set(U"5"); // before the track's program change
    CHECK(p.ws.message().empty());

    p.runFor(1s);
    set(U"6");
    CHECK(p.ws.message() == "Sent at the next play: the track's own program change is in force");
    CHECK(p.document.song().tracks()[0]->programSetup.program == 6);
}

TEST_CASE("the transport shows the tempo at the marker, and at the playhead while playing")
{
    Playing p;
    p.ws.resize(120 * kCell.width, 30 * kCell.height, kCell); // the default minimum window
    CHECK(contains(p.status(), U"120 BPM"));
    p.type(U"'a"); // the demo song's mark, in its slower bridge: the cursor isn't the transport's
    CHECK(contains(p.status(), U"120 BPM"));

    const auto bridge = p.document.song().marks().front().tick;
    p.player.setMarker(bridge);
    p.type(U"gg");
    CHECK(contains(p.status(), U" 96 BPM")); // padded to the song's widest tempo
    const auto section = p.document.song().sectionAt(bridge);
    CHECK(contains(p.status(), decodeUtf8(p.document.song().sections()[section]->name())));
    p.press(Key::typed(U' '));
    p.runFor(100ms);
    CHECK(contains(p.status(), U"96 BPM"));
}

TEST_CASE("the transport keeps its place, playing or stopped, with a message after it")
{
    Playing p;
    const auto runs = [&]
    {
        const auto frame = p.ws.frame();
        return frame.back().text;
    };
    const auto at = [](const std::vector<TextRun>& text, std::u32string_view start)
    {
        const auto run = std::ranges::find_if(text, [&](const TextRun& candidate)
                                              { return candidate.text.starts_with(start); });
        return run == text.end() ? -1 : run->x;
    };
    const auto stopped = runs();
    const int glyph = at(stopped, U"■");
    REQUIRE(glyph > 0);
    p.press(Key::typed(U' '));
    p.runFor(100ms);
    const auto playing = runs();
    CHECK(at(playing, U"▶") == glyph);
    // What follows the glyph is as long, playing or stopped, as each part is padded.
    const auto after = [&](const std::vector<TextRun>& text)
    {
        const auto run = std::ranges::find_if(text, [&](const TextRun& candidate)
                                              { return candidate.x > glyph; });
        return run == text.end() ? std::size_t{0} : run->text.size();
    };
    CHECK(after(playing) == after(stopped));
    // A message goes after it, leaving it shown.
    p.type(U"yy");
    REQUIRE_FALSE(p.ws.message().empty());
    CHECK(at(runs(), U"▶") == glyph);
    std::u32string shown; // in the status line, or above it if too long for it
    for (const auto& region : p.ws.frame())
        for (const auto& run : region.text)
            shown += run.text;
    CHECK(contains(shown, decodeUtf8(p.ws.message())));
    p.press(Key::typed(U' '));
}

TEST_CASE("the mode takes the same room whichever it is, so the transport after it stays put")
{
    Playing p;
    const auto glyphAt = [&]
    {
        const auto frame = p.ws.frame();
        const auto& text = frame.back().text;
        const auto run = std::ranges::find_if(text, [](const TextRun& candidate)
                                              { return candidate.text.starts_with(U"■"); });
        return run == text.end() ? -1 : run->x;
    };
    const int normal = glyphAt();
    REQUIRE(normal > 0);
    p.type(U"v");
    REQUIRE(p.ws.mode() == Mode::Visual);
    CHECK(glyphAt() == normal);
}

TEST_CASE("a track's port that wasn't found is in the warning colour, and explained in the "
          "inspector")
{
    core::Document document{demoSong()};
    document.edit("Set a port", {},
                  [&](core::Song& song)
                  {
                      song.updateTrack(song.tracks()[0]->id(),
                                       [](core::Track& track) { track.port = "Gone"; });
                  });
    PortTable table;
    table.detected(PortDirection::Output, std::vector<std::string>{"Synth"});
    table.setEnabled(PortDirection::Output, "Synth", true);
    table.setDefault(PortDirection::Output, "Synth");
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace ws{registry, document};
    ws.resize(120 * kCell.width, 24 * kCell.height, kCell);
    ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    for (const char32_t key : std::u32string_view{U"gtj"})
    {
        ws.handle(Key::typed(key), 0);
    }
    const auto find = [&](std::u32string_view wanted) -> std::optional<TextRun>
    {
        for (const auto& region : ws.frame())
        {
            for (const auto& run : region.text)
            {
                if (run.text.find(wanted) != std::u32string::npos)
                {
                    return run;
                }
            }
        }
        return std::nullopt;
    };
    REQUIRE(find(U"Gone"));
    CHECK(find(U"Gone")->style.foreground == Role::Warning);
    CHECK_FALSE(find(U"Output not found"));

    for (const char32_t key : U"k" + toColumn(TrackField::Port))
    {
        ws.handle(Key::typed(key), 0);
    }
    REQUIRE(find(U"Output not found"));
    bool warned = false;
    for (const auto& region : ws.frame())
    {
        for (const auto& run : region.text)
        {
            warned = warned || (run.text == U"Gone" && run.style.foreground == Role::Warning);
        }
    }
    CHECK(warned);
}

TEST_CASE("bypassing playback quantise plays every track as stored, and says so")
{
    Playing p;
    CHECK(p.status().find(U"Quantise bypassed") == std::u32string::npos);
    p.type(U",Tq");
    CHECK(p.player.bypassQuantize());
    CHECK(p.status().find(U"Quantise bypassed") != std::u32string::npos);
    p.press(Key::typed(U'j'));
    p.type(U",Tq");
    CHECK_FALSE(p.player.bypassQuantize());
    CHECK(p.status().find(U"Quantise bypassed") == std::u32string::npos);
}

TEST_CASE("MIDI input is taken from the engine when it arrives, and again shortly after")
{
    Playing p;
    CHECK_FALSE(p.ws.deadline());
    const std::array<std::uint8_t, 3> on{0x90, 60, 100};
    const std::array<std::uint8_t, 3> off{0x80, 60, 0};
    REQUIRE(p.engine.receive(core::InputId{0}, on, p.clock.time));
    test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
    p.ws.inputWoken(p.now);
    CHECK_FALSE(p.engine.nextReceived()); // taken
    CHECK(p.ws.deadline() == p.now + Workspace::kInputSettle);

    // What the engine passes back after the wake-up is taken at that deadline, which then goes.
    REQUIRE(p.engine.receive(core::InputId{0}, off, p.clock.time));
    test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
    p.now += Workspace::kInputSettle;
    p.ws.wake(p.now);
    CHECK_FALSE(p.engine.nextReceived());
    REQUIRE(p.ws.deadline()); // input light still needs to expire
    p.ws.wake(*p.ws.deadline());
    CHECK_FALSE(p.ws.deadline());
    CHECK(p.ws.message().empty());
}

TEST_CASE("MIDI input lost for want of taking it is reported once")
{
    Playing p;
    const std::array<std::uint8_t, 3> cc{0xB0, 1, 64};
    const auto sent = core::Engine::kMaxStamped + 100;
    for (std::size_t i = 0; i < sent; ++i)
    {
        REQUIRE(p.engine.receive(core::InputId{0}, cc, p.clock.time));
        if (i % core::Engine::kInputBatch == core::Engine::kInputBatch - 1) // let each batch drain
        {
            test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
        }
    }
    test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
    REQUIRE(p.player.inputLost() > 0);
    CHECK(p.ws.inputWoken(p.now));
    CHECK(p.ws.message() == "MIDI input lost: " + std::to_string(p.player.inputLost()) +
                                " messages, arriving faster than it was taken");
    CHECK(p.ws.messageKind() == MessageKind::Warning);
    CHECK_FALSE(p.ws.inputWoken(p.now)); // nothing more lost
}

TEST_CASE("an idle workspace collects every bounded input batch before stopping its timer")
{
    Playing p;
    const std::array<std::uint8_t, 3> cc{0xB0, 1, 64};
    for (std::size_t n = 0; n < core::Engine::kInputBatch * 2; ++n)
        REQUIRE(p.engine.receive(core::InputId{0}, cc, p.clock.time));
    p.ws.inputWoken(p.now);
    // The engine may not yet have run when the first settle deadline arrives.
    p.now += Workspace::kInputSettle;
    p.ws.wake(p.now);
    REQUIRE(p.ws.deadline());
    CHECK(*p.ws.deadline() == p.now + Workspace::kInputSettle);
    p.engine.step();
    CHECK(p.player.inputPending());
    p.now += Workspace::kInputSettle;
    p.ws.wake(p.now);
    CHECK_FALSE(p.engine.nextReceived());
    REQUIRE(p.ws.deadline()); // still has an engine backlog
    p.engine.step();
    CHECK_FALSE(p.player.inputPending());
    p.now += Workspace::kInputSettle;
    p.ws.wake(p.now);
    CHECK_FALSE(p.engine.nextReceived()); // final batch was collected too
    REQUIRE(p.ws.deadline());             // the light expires after the final batch
    p.ws.wake(*p.ws.deadline());
    CHECK_FALSE(p.ws.deadline()); // back to an idle window
    CHECK(p.ws.message().empty());
}

TEST_CASE("input activity and the last note are visible in every view and the light expires")
{
    Playing p;
    const std::array<std::uint8_t, 3> on{0x90, 60, 96};
    REQUIRE(p.engine.receive(core::InputId{0}, on, p.clock.time));
    p.engine.step();
    CHECK(p.ws.inputWoken(p.now));
    CHECK(contains(p.status(), U"In  1 C4      96"));
    const auto light = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text == U"In")
                return run.style.foreground;
        return Role::Warning;
    };
    CHECK(light() == Role::Accent);
    p.ws.show(ViewKind::Tracks);
    CHECK(contains(p.status(), U"In  1 C4      96"));
    p.ws.show(ViewKind::PianoRoll);
    CHECK(contains(p.status(), U"In  1 C4      96"));
    p.ws.show(ViewKind::Arrangement);
    CHECK(contains(p.status(), U"In  1 C4      96"));
    // The short collection deadline precedes the light's deadline.
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    CHECK(light() == Role::Dim);
    CHECK(contains(p.status(), U"In  1 C4      96"));
    CHECK_FALSE(p.ws.deadline());

    // Any channel message but a note-off, with its channel, keeping what follows still.
    const auto rewindX = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text.starts_with(U"Rewind"))
                return run.x;
        return -1;
    };
    const auto before = rewindX();
    const auto in = [&](std::vector<std::uint8_t> bytes)
    {
        REQUIRE(p.engine.receive(core::InputId{0}, bytes, p.clock.time));
        p.engine.step();
        p.ws.inputWoken(p.now);
    };
    in({0x80, 60, 0});
    CHECK(contains(p.status(), U"In  1 C4      96"));
    in({0xB9, 64, 127});
    CHECK(contains(p.status(), U"In 10 CC64   127"));
    CHECK(rewindX() == before);
    in({0xE0, 0x7F, 0x7F});
    CHECK(contains(p.status(), U"In  1 Bend +8191"));
    CHECK(rewindX() == before);
}

TEST_CASE("global Thru is a machine control and held notes can end while it is off")
{
    Playing p;
    const auto id = p.document.song().tracks()[0]->id();
    p.document.edit("Arm", {}, [&](core::Song& song)
                    { song.updateTrack(id, [](core::Track& track) { track.arm = true; }); });
    p.player.update(p.document.song());
    const auto before = p.document.song();
    p.engine.step();
    p.sink.sent.clear();
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, 60, 96},
                             p.clock.time));
    p.engine.step();
    REQUIRE(p.sink.sent.size() == 1);
    p.type(U",Tt");
    CHECK(p.document.song() == before);
    CHECK(contains(p.status(), U"Thru off"));
    p.sink.sent.clear();
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, 62, 96},
                             p.clock.time));
    REQUIRE(
        p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x80, 60, 0}, p.clock.time));
    p.engine.step();
    REQUIRE(p.sink.sent.size() == 1);
    CHECK((p.sink.sent[0].bytes[0] & 0xF0) == 0x80);
    p.type(U",Tt");
    CHECK(p.ws.message() == "Thru on");
    CHECK_FALSE(contains(p.status(), U"Thru off"));
}

TEST_CASE("an unavailable input is in the warning colour with an inspector warning, and one "
          "found by a part of its name is not")
{
    Playing p;
    const auto id = p.document.song().tracks()[0]->id();
    p.document.edit("Input", {}, [&](core::Song& song)
                    { song.updateTrack(id, [](core::Track& track) { track.input = "Keys"; }); });
    PortTable table;
    table.detected(PortDirection::Input, std::vector<std::string>{"My Keys"});
    table.setEnabled(PortDirection::Input, "My Keys", true);
    table.detected(PortDirection::Input, std::vector<std::string>{}); // unplugged
    p.ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    p.ws.setInputPorts([] { return std::vector<std::string>{"My Keys"}; });
    p.type(U"gt");
    p.type(U"j"); // the cursor off the cell, to see its colour
    p.type(toColumn(TrackField::Input));
    const auto warning = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.front().text)
            if (run.text.starts_with(U"Keys") && run.y == kCell.height)
                return run.style.foreground == Role::Warning;
        return false;
    };
    const auto warned = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& region : frame)
            for (const auto& run : region.text)
                if (run.text.find(U"Input not found") != std::u32string::npos)
                    return true;
        return false;
    };
    CHECK(warning());
    p.type(U"k");
    CHECK(warned());
    table.detected(PortDirection::Input, std::vector<std::string>{"My Keys"});
    CHECK_FALSE(warned());
    p.type(U"j");
    CHECK_FALSE(warning());
    p.type(U"k");
    p.press(Key::of(NamedKey::Enter));
    const auto frame = p.ws.frame();
    for (const auto& region : frame)
        for (const auto& run : region.text)
            CHECK(run.text.find(U"not found") == std::u32string::npos);
}
