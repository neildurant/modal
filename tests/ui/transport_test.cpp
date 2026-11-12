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

// How a status line indicator shows, found by its word: dim, lit, in the warning colour, or
// not there at all. Wide enough for them all.
enum class Shown : std::uint8_t
{
    Absent,
    Off,
    Lit,
    Warning,
};
Shown indicator(Playing& p, std::u32string_view word)
{
    p.ws.resize(220 * kCell.width, 24 * kCell.height, kCell);
    const auto frame = p.ws.frame();
    for (const auto& run : frame.back().text)
    {
        if (run.text.find(word) != std::u32string::npos && run.style.background != Role::Bar)
        {
            return run.style.background == Role::Indicator ? Shown::Off
                   : run.style.background == Role::Warning ? Shown::Warning
                                                           : Shown::Lit;
        }
    }
    return Shown::Absent;
}

} // namespace

TEST_CASE("Space plays from the marker and stops where it is; with rewind, the marker stays")
{
    Playing p;
    REQUIRE(p.ws.rewind());
    p.type(U"5j");
    CHECK(p.ws.deadline() == std::nullopt);
    CHECK(indicator(p, U"REWIND") == Shown::Lit);

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
    CHECK(indicator(p, U"REWIND") == Shown::Off);
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
    CHECK(indicator(p, U"Q BYPASS") == Shown::Off);
    p.type(U",Tq");
    CHECK(p.player.bypassQuantize());
    CHECK(indicator(p, U"Q BYPASS") == Shown::Warning);
    p.press(Key::typed(U'j'));
    p.type(U",Tq");
    CHECK_FALSE(p.player.bypassQuantize());
    CHECK(indicator(p, U"Q BYPASS") == Shown::Off);
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
    p.ws.resize(220 * kCell.width, 24 * kCell.height, kCell);
    const std::array<std::uint8_t, 3> on{0x90, 60, 96};
    REQUIRE(p.engine.receive(core::InputId{0}, on, p.clock.time));
    p.engine.step();
    CHECK(p.ws.inputWoken(p.now));
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    const auto light = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text == U" IN ")
                return run.style.background;
        return Role::Warning;
    };
    CHECK(light() == Role::IndicatorInput);
    p.ws.show(ViewKind::Tracks);
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    p.ws.show(ViewKind::PianoRoll);
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    p.ws.show(ViewKind::Arrangement);
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    // The short collection deadline precedes the light's deadline.
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    CHECK(light() == Role::Indicator);
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    CHECK_FALSE(p.ws.deadline());

    // Any channel message but a note-off, with its channel, keeping what follows still.
    const auto fileX = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text.starts_with(U" No file"))
                return run.x;
        return -1;
    };
    const auto before = fileX();
    const auto in = [&](std::vector<std::uint8_t> bytes)
    {
        REQUIRE(p.engine.receive(core::InputId{0}, bytes, p.clock.time));
        p.engine.step();
        p.ws.inputWoken(p.now);
    };
    in({0x80, 60, 0});
    CHECK(contains(p.status(), U" IN   1 C4      96 "));
    in({0xB9, 64, 127});
    CHECK(contains(p.status(), U" IN  10 CC64   127 "));
    CHECK(fileX() == before);
    in({0xE0, 0x7F, 0x7F});
    CHECK(contains(p.status(), U" IN   1 Bend +8191 "));
    CHECK(fileX() == before);
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
    CHECK(indicator(p, U"THRU") == Shown::Off);
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
    CHECK(indicator(p, U"THRU") == Shown::Lit);
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

namespace
{
// The track armed, and its events, for the recording tests.
const core::Track* armed(const core::Document& document)
{
    for (const auto& track : document.song().tracks())
        if (track->arm)
            return track.get();
    return nullptr;
}

bool hasPitch(const core::Song& song, core::TrackId id, std::uint8_t pitch)
{
    for (const auto& event : song.events(id).all())
        if (const auto* note = std::get_if<core::Note>(&event.data); note && note->pitch == pitch)
            return true;
    return false;
}

// Plays a note in on the keyboard, held for a while.
void playNote(Playing& p, std::uint8_t pitch)
{
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, pitch, 100},
                             p.clock.time));
    p.runFor(200ms);
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x80, pitch, 0},
                             p.clock.time));
    p.runFor(100ms);
}
} // namespace

TEST_CASE("R records onto the armed track after a count-in, Space stops, and it's one undo step")
{
    Playing p;
    p.type(U"R");
    CHECK(p.ws.message() == "No track is armed: , r a arms a track");
    p.type(U",ra");
    const auto* track = armed(p.document);
    REQUIRE(track != nullptr);
    const auto id = track->id();
    const auto name = track->name;
    CHECK(indicator(p, U"ARM 1") == Shown::Lit);
    CHECK(indicator(p, U"REPLACE") == Shown::Lit);

    p.type(U"R");
    CHECK(p.player.recording());
    CHECK(p.ws.message() == "Recording onto " + name);
    CHECK(contains(p.status(), U"● COUNT  ")); // counting in, then recording
    const auto x = [&](std::u32string_view starts)
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text.starts_with(starts))
                return run.x;
        return -1;
    };
    const auto place = x(U"1:");
    p.type(U"R");
    CHECK(p.ws.message() == "Already recording: Space stops");
    // The count-in clicks on the default output's channel 10, a bar at the song's tempo.
    p.runFor(2100ms);
    CHECK(std::ranges::any_of(p.sink.sent, [](const auto& sent)
                              { return !sent.bytes.empty() && sent.bytes[0] == 0x99; }));
    CHECK(contains(p.status(), U"● REC    "));
    CHECK(x(U"1:") == place); // the same width, so the place doesn't move
    playNote(p, 99);
    // Shown as it's played, and put into the song once stopped.
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK_FALSE(hasPitch(p.document.underlying(), id, 99));
    p.type(U" ");
    p.runFor(10ms);
    CHECK_FALSE(p.player.recording());
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK(p.ws.message() == "Recorded onto " + name);
    CHECK_FALSE(contains(p.status(), U"● REC"));
    p.type(U"u");
    CHECK(p.ws.message() == "Undid: Record onto " + name);
    CHECK_FALSE(hasPitch(p.document.song(), id, 99));
}

TEST_CASE("the leader then r and m switches the song between replace and overdub")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    const auto before = p.document.song().events(id).size();
    REQUIRE(before > 0);
    p.type(U",rm");
    CHECK(p.document.song().recordMode == core::RecordMode::Overdub);
    CHECK(p.ws.message() == "Overdub: what's recorded is added to what's there");
    CHECK(indicator(p, U"OVERDUB") == Shown::Lit);
    p.type(U"R");
    CHECK(p.ws.message().starts_with("Overdubbing onto "));
    p.runFor(2100ms);
    CHECK(contains(p.status(), U"● ODUB"));
    playNote(p, 99);
    p.runFor(4s);
    p.type(U" ");
    p.runFor(10ms);
    // Overdubbed: what was there stays.
    CHECK(p.document.song().events(id).size() == before + 1);
    p.type(U"u");
    p.type(U",rm");
    CHECK(p.document.song().recordMode == core::RecordMode::Replace);
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    p.runFor(4s);
    p.type(U" ");
    p.runFor(10ms);
    // Replaced: what was in the recorded range went.
    CHECK(p.document.song().events(id).size() < before + 1);
    CHECK(hasPitch(p.document.song(), id, 99));
}

TEST_CASE("recording isn't started while playing, and stopping in the count-in records nothing")
{
    Playing p;
    p.type(U",ra");
    p.type(U" ");
    p.type(U"R");
    CHECK(p.ws.message() == "Stop first to record");
    CHECK_FALSE(p.player.recording());
    p.type(U" ");
    p.runFor(10ms);
    const auto version = p.document.version();
    p.type(U"R");
    p.runFor(500ms);
    p.press(Key::typed(U' ', kCtrl)); // playing from elsewhere would end the pass
    CHECK(p.ws.message() == "Recording: Space stops it first");
    p.type(U" ");
    p.runFor(10ms);
    CHECK(p.ws.message() == "Nothing recorded: stopped before the record point");
    CHECK(p.document.version() == version);
}

TEST_CASE("recording carries on past the song's end, which grows to hold the take")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    const auto end = p.document.song().end();
    p.type(U"G");
    p.type(U"m ");
    p.type(U"R");
    p.runFor(30s);
    CHECK(p.player.recording());
    playNote(p, 99);
    p.ws.show(ViewKind::Arrangement); // shown as recording stops, it shows the song grown
    p.type(U" ");
    p.runFor(10ms);
    CHECK(p.document.song().end() > end);
    CHECK(p.ws.arrangement().page().end() == p.document.song().end());
    CHECK(hasPitch(p.document.song(), id, 99));
}

namespace
{
// The note on a pitch in a track's events, if there's one.
std::optional<core::Event> noteOn(const core::Song& song, core::TrackId id, std::uint8_t pitch)
{
    for (const auto& event : song.events(id).all())
        if (const auto* note = std::get_if<core::Note>(&event.data); note && note->pitch == pitch)
            return event;
    return std::nullopt;
}

bool has(const core::Song& song, core::TrackId id, const core::Event& event)
{
    return std::ranges::find(song.events(id).all(), event) != song.events(id).all().end();
}
} // namespace

TEST_CASE("the take shows as it's played, a held note growing with the playhead and what it "
          "replaces going as the playhead passes")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    const auto old = p.document.song().events(id).all();
    p.type(U"R");
    p.runFor(2100ms); // the count-in
    REQUIRE(p.player.recordPhase() == core::Player::RecordPhase::Recording);
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, 99, 100},
                             p.clock.time));
    p.runFor(300ms);
    const auto first = noteOn(p.document.song(), id, 99);
    REQUIRE(first.has_value());
    const auto firstLength = std::get<core::Note>(first->data).duration;
    CHECK(firstLength > 0);
    CHECK_FALSE(noteOn(p.document.underlying(), id, 99).has_value());

    // What's shown is what stopping now would make: old events before the playhead gone,
    // those after it kept.
    const auto shownAt = [&](core::Tick at)
    {
        std::size_t gone = 0;
        for (const auto& event : old)
        {
            const bool shown = has(p.document.song(), id, event);
            if (event.tick < at)
            {
                CHECK_FALSE(shown);
                gone += shown ? 0 : 1;
            }
            else if (event.tick > at)
            {
                CHECK(shown);
            }
            CHECK(has(p.document.underlying(), id, event));
        }
        return gone;
    };
    const auto goneFirst = shownAt(*p.player.position());
    p.runFor(3s);
    const auto later = noteOn(p.document.song(), id, 99);
    REQUIRE(later.has_value());
    CHECK(std::get<core::Note>(later->data).duration > firstLength);
    const auto goneLater = shownAt(*p.player.position());
    CHECK(goneLater > goneFirst);

    p.type(U" ");
    p.runFor(10ms);
    // Stopped with the note still held: it ends where recording stopped, as shown.
    const auto taken = noteOn(p.document.song(), id, 99);
    REQUIRE(taken.has_value());
    CHECK(p.document.song() == p.document.underlying());
    CHECK(std::get<core::Note>(taken->data).duration >= std::get<core::Note>(later->data).duration);
}

TEST_CASE("a note and pedal still held when recording stops end there")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    p.type(U"R");
    p.runFor(2100ms);
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0xB0, 64, 127},
                             p.clock.time));
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, 98, 100},
                             p.clock.time));
    p.runFor(500ms);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE_FALSE(p.player.recording());
    const auto note = noteOn(p.document.song(), id, 98);
    REQUIRE(note.has_value());
    const auto end = note->tick + std::get<core::Note>(note->data).duration;
    CHECK(has(p.document.song(), id, core::Event{end, core::ControlChange{64, 0}}));
    // Let go after stopping: nothing more is recorded.
    const auto version = p.document.version();
    REQUIRE(
        p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x80, 98, 0}, p.clock.time));
    REQUIRE(
        p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0xB0, 64, 0}, p.clock.time));
    p.runFor(100ms);
    CHECK(p.document.version() == version);
}

TEST_CASE("replacing, the track being recorded is silent from the record point; overdubbing, and "
          "after the pass, it's heard")
{
    Playing p;
    // A track with notes in its first two bars, off the click's channel.
    const auto& song = p.document.song();
    const auto tracks = song.tracks();
    const auto busy = std::ranges::find_if(
        tracks,
        [&](const auto& candidate)
        {
            return candidate->channel != song.metronome.channel &&
                   std::ranges::any_of(
                       song.events(candidate->id()).all(), [&](const core::Event& event)
                       { return event.isNote() && event.tick < 2 * 4 * song.ppqn().quarter(); });
        });
    REQUIRE(busy != tracks.end());
    for (auto i = tracks.begin(); i != busy; ++i)
        p.press(Key::of(NamedKey::PageDown, kCtrl));
    p.type(U",ra");
    const auto* track = armed(p.document);
    REQUIRE(track != nullptr);
    REQUIRE(track->channel != p.document.song().metronome.channel);
    const auto noteOn = static_cast<std::uint8_t>(0x90 | (track->channel - 1));
    // The note-ons sent on its channel while the song plays for a while.
    const auto heard = [&](std::u32string_view keys)
    {
        p.type(keys);
        p.runFor(2100ms); // past a count-in, if there is one
        const auto from = p.sink.sent.size();
        p.runFor(4s);
        const auto count = std::ranges::count_if(
            p.sink.sent.begin() + static_cast<std::ptrdiff_t>(from), p.sink.sent.end(),
            [&](const auto& sent)
            { return sent.bytes.size() == 3 && sent.bytes[0] == noteOn && sent.bytes[2] > 0; });
        p.type(U" ");
        p.runFor(10ms);
        return count;
    };
    CHECK(heard(U"R") == 0);
    CHECK(p.ws.message() == "Recorded onto " + track->name); // the old notes gone
    p.type(U"u");
    const auto played = heard(U" ");
    CHECK(played > 0);
    p.type(U",rm"); // overdub
    CHECK(heard(U"R") > 0);
}

TEST_CASE("while recording, edits and undo that would change a track being recorded are refused, "
          "and other tracks can still be edited")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    const auto name = armed(p.document)->name;
    const auto other = p.document.song().tracks()[1]->id();
    REQUIRE(other != id);
    // x on an event of the track, not on a section's, tempo's or label's row.
    const auto deleteEvent = [&]
    {
        for (int i = 0; i < 50 && p.view.list().rows()[p.view.row()].kind != RowKind::Event; ++i)
            p.type(U"j");
        REQUIRE(p.view.list().rows()[p.view.row()].kind == RowKind::Event);
        p.type(U"x");
    };
    deleteEvent(); // a step to undo, on the armed track
    const auto before = p.document.underlying().events(id);
    const auto refusal = name + " is being recorded: change it once recording stops";

    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    deleteEvent();
    CHECK(p.ws.message() == refusal);
    CHECK(p.document.underlying().events(id) == before);
    p.type(U"u");
    CHECK(p.ws.message() == refusal);
    CHECK(p.document.underlying().events(id) == before);
    CHECK(p.player.recording());

    p.press(Key::of(NamedKey::PageDown, kCtrl)); // the next track, not being recorded
    const auto otherBefore = p.document.underlying().events(other);
    deleteEvent();
    CHECK(p.document.underlying().events(other).size() + 1 == otherBefore.size());
    CHECK(p.document.song().events(other) == p.document.underlying().events(other));

    p.type(U" ");
    p.runFor(10ms);
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK(p.document.song().events(other).size() + 1 == otherBefore.size());
    // Stopped, the track can be changed again, and undo goes back through both.
    p.type(U"u");
    CHECK_FALSE(hasPitch(p.document.song(), id, 99));
    p.type(U"u");
    CHECK(p.document.song().events(other) == otherBefore);
    p.type(U"u");
    CHECK(p.document.song().events(id).size() == before.size() + 1);
}

TEST_CASE("the leader then m sets the metronome and count-in, and :countin and :preroll their "
          "lengths, each an undo step")
{
    Playing p;
    const auto& song = [&]() -> const core::Song& { return p.document.song(); };
    using When = core::MetronomeSettings::When;
    using Mode = core::CountInSettings::Mode;
    REQUIRE(song().metronome.when == When::Recording);
    p.type(U",mm");
    CHECK(song().metronome.when == When::Always);
    CHECK(p.ws.message() == "Metronome always");
    p.type(U",mm");
    CHECK(song().metronome.when == When::Off);
    p.type(U",mm");
    CHECK(song().metronome.when == When::Recording);
    p.type(U"u");
    CHECK(song().metronome.when == When::Off);

    REQUIRE(song().countIn.mode == Mode::CountIn);
    p.type(U",mc");
    CHECK(song().countIn.mode == Mode::PreRoll);
    CHECK(p.ws.message() == "Pre-roll 1 bar");
    p.type(U",mc");
    CHECK(song().countIn.mode == Mode::None);
    CHECK(p.ws.message() == "No count-in");

    CHECK(p.registry.runLine("countin 2").message == "Count-in 2 bars");
    CHECK(song().countIn.mode == Mode::CountIn);
    CHECK(song().countIn.bars == 2);
    CHECK(p.registry.runLine("countin 3 beats").message == "Count-in 3 beats");
    CHECK(song().countIn.beats == 3);
    CHECK(p.registry.runLine("countin").message == "Count-in 3 beats");
    CHECK(p.registry.runLine("preroll 2").message == "Pre-roll 2 bars");
    CHECK(song().countIn.preRollBars == 2);
    CHECK(p.registry.runLine("preroll 3 beats").message == "Pre-roll 3 beats");
    CHECK(song().countIn.preRollBeats == 3);
    CHECK(song().countIn.preRollBars == 2);
    CHECK(p.registry.runLine("preroll 2").message == "Pre-roll 2 bars");
    CHECK(song().countIn.preRollBeats == 0);
    CHECK(p.registry.runLine("countin off").message == "No count-in");
    CHECK(p.registry.runLine("countin 0").message == "Bars should be from 1 to 99");
    CHECK(p.registry.runLine("countin two").message == "countin takes BARS, BEATS beats, or off");
    CHECK(p.registry.runLine("preroll 2 bats").message ==
          "preroll takes BARS, BEATS beats, or off");
}

TEST_CASE("with tracks armed, the status line says what comes before recording, and PRE shows "
          "through a pre-roll")
{
    Playing p;
    p.ws.resize(140 * kCell.width, 24 * kCell.height, kCell);
    CHECK(indicator(p, U"ARM") == Shown::Off);
    CHECK(indicator(p, U"COUNT 1 BAR") == Shown::Off);
    p.type(U",ra");
    CHECK(indicator(p, U"ARM 1") == Shown::Lit);
    CHECK(indicator(p, U"COUNT 1 BAR") == Shown::Lit);
    p.registry.runLine("preroll 1");
    CHECK(indicator(p, U"PRE 1 BAR") == Shown::Lit);
    p.registry.runLine("preroll 3 beats");
    CHECK(indicator(p, U"PRE 3 BEATS") == Shown::Lit);
    p.registry.runLine("preroll 1");
    // Narrower, in short words.
    p.ws.resize(100 * kCell.width, 24 * kCell.height, kCell);
    CHECK(contains(p.status(), U" PRE 1 "));
    CHECK(contains(p.status(), U" REPL "));
    p.ws.resize(140 * kCell.width, 24 * kCell.height, kCell);
    // Recording from the third bar, after a bar of the song before it.
    p.player.setMarker(2 * p.document.song().ppqn().whole());
    p.type(U"R");
    p.runFor(10ms);
    CHECK(contains(p.status(), U"● PRE    "));
    p.runFor(2100ms);
    CHECK(contains(p.status(), U"● REC    "));
    p.type(U" ");
    p.runFor(10ms);
}

TEST_CASE("the status line says when the click has nowhere to go")
{
    Playing p;
    p.ws.resize(140 * kCell.width, 24 * kCell.height, kCell);
    PortTable table;
    table.detected(PortDirection::Output, std::vector<std::string>{"Synth"});
    table.setEnabled(PortDirection::Output, "Synth", true);
    p.ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    CHECK(indicator(p, U"CLICK") == Shown::Off);
    p.type(U",ra");
    CHECK(indicator(p, U"CLICK") == Shown::Warning);
    table.setDefault(PortDirection::Output, "Synth");
    CHECK(indicator(p, U"CLICK") == Shown::Lit);
}

namespace
{

// The note-ons sent since the sink was last cleared, after letting the engine run, as
// pitch and channel.
std::vector<std::pair<int, int>> notesSent(Playing& p)
{
    p.runFor(5ms);
    std::vector<std::pair<int, int>> notes;
    for (const auto& sent : p.sink.sent)
    {
        if (sent.bytes.size() == 3 && (sent.bytes[0] & 0xF0) == 0x90 && sent.bytes[2] > 0)
            notes.emplace_back(sent.bytes[1], sent.bytes[0] & 0x0F);
    }
    p.sink.sent.clear();
    return notes;
}

// Shows the piano roll on a track of the demo song with notes.
const core::Track& onTrackWithNotes(Playing& p)
{
    p.ws.show(ViewKind::PianoRoll);
    auto& roll = p.ws.pianoRoll();
    const auto& song = p.document.song();
    while (song.events(song.tracks()[roll.track()]->id()).empty())
        p.press(Key::of(NamedKey::PageDown, kCtrl));
    return *song.tracks()[roll.track()];
}

} // namespace

TEST_CASE("audition plays the note the piano roll's cursor lands on, once, as playback would")
{
    Playing p;
    const auto& track = onTrackWithNotes(p);
    auto& roll = p.ws.pianoRoll();
    REQUIRE(p.ws.audition());
    notesSent(p);
    // Tab to the next note, which is heard on the track's channel, transposed as it plays.
    p.press(Key::of(NamedKey::Tab));
    const auto index = roll.currentNote();
    REQUIRE(index);
    const auto& song = p.document.song();
    const auto& event = song.events(track.id())[*index];
    const int pitch = std::get<core::Note>(event.data).pitch + track.transpose;
    CHECK(notesSent(p) == std::vector<std::pair<int, int>>{{pitch, track.channel - 1}});
    // A key that leaves the cursor on it plays nothing more.
    p.press(Key::of(NamedKey::Escape));
    CHECK(notesSent(p).empty());
    // The next is heard.
    p.press(Key::of(NamedKey::Tab));
    REQUIRE(roll.currentNote() != index);
    CHECK(notesSent(p).size() == 1);
}

TEST_CASE("audition is off with , T a, said in the status line, and silent while selecting")
{
    Playing p;
    p.ws.resize(140 * kCell.width, 24 * kCell.height, kCell);
    onTrackWithNotes(p);
    notesSent(p);
    // The note the piano roll opened on, if any, isn't heard at the next key.
    p.type(U",Ta");
    CHECK(notesSent(p).empty());
    CHECK_FALSE(p.ws.audition());
    CHECK(indicator(p, U"AUDITION") == Shown::Off);
    p.press(Key::of(NamedKey::Tab));
    REQUIRE(p.ws.pianoRoll().currentNote());
    CHECK(notesSent(p).empty());
    p.type(U",Ta");
    CHECK(p.ws.audition());
    CHECK(indicator(p, U"AUDITION") == Shown::Lit);
    // A Visual selection growing over notes is silent.
    p.type(U"v");
    p.press(Key::of(NamedKey::Tab));
    p.press(Key::of(NamedKey::Tab));
    CHECK(notesSent(p).empty());
}

TEST_CASE(
    "audition plays a note added with o, and one moved to a new pitch, but not one moved in time")
{
    Playing p;
    const auto& track = onTrackWithNotes(p);
    auto& roll = p.ws.pianoRoll();
    notesSent(p);
    // An empty pitch at the cursor's time: o adds the track's default note there.
    p.type(U"0");
    while (roll.currentNote())
        p.press(Key::typed(U'k'));
    notesSent(p);
    p.type(U"o");
    REQUIRE(roll.currentNote());
    const int pitch = roll.pitch() + track.transpose;
    CHECK(notesSent(p) == std::vector<std::pair<int, int>>{{pitch, track.channel - 1}});
    p.press(Key::typed(U'k', kAlt));
    CHECK(notesSent(p) == std::vector<std::pair<int, int>>{{pitch + 1, track.channel - 1}});
    p.press(Key::typed(U'l', kAlt));
    CHECK(notesSent(p).empty());
    // Undo isn't heard either.
    p.type(U"u");
    CHECK(notesSent(p).empty());
}

TEST_CASE("audition is silent while playing, as through a count-in, when only the click sounds")
{
    Playing p;
    onTrackWithNotes(p);
    const auto& click = p.document.song().metronome;
    p.type(U",ra");
    p.type(U"R");
    p.runFor(10ms);
    REQUIRE(p.player.recordPhase() == core::Player::RecordPhase::CountIn);
    notesSent(p);
    p.press(Key::of(NamedKey::Tab));
    REQUIRE(p.ws.pianoRoll().currentNote());
    for (const auto& [pitch, channel] : notesSent(p))
    {
        const bool clicked = channel == click.channel - 1 &&
                             (pitch == click.downbeatNote || pitch == click.beatNote);
        CHECK(clicked);
    }
    p.type(U" ");
    p.runFor(10ms);
}

TEST_CASE("the status line's indicators keep their places whatever they say, dim or lit")
{
    Playing p;
    p.ws.resize(220 * kCell.width, 24 * kCell.height, kCell);
    const auto xOf = [&](std::u32string_view word)
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text.find(word) != std::u32string::npos)
                return run.x;
        return -1;
    };
    const auto places = [&]
    {
        return std::array{xOf(U"CLICK"), xOf(U"THRU"), xOf(U"AUDITION"), xOf(U"REWIND"),
                          xOf(U" IN ")};
    };
    const auto before = places();
    CHECK(before[0] > 0);
    p.type(U",ra");
    CHECK(places() == before);
    p.type(U",rm");
    CHECK(indicator(p, U"OVERDUB") == Shown::Lit);
    CHECK(places() == before);
    p.registry.runLine("preroll 2");
    CHECK(places() == before);
    p.registry.runLine("countin off");
    CHECK(indicator(p, U"NO COUNT") == Shown::Off);
    CHECK(places() == before);
    p.type(U",Ta");
    CHECK(places() == before);
}

TEST_CASE("the message row is always there, holding messages, hints, selections and the prompt, "
          "so the status line under it never moves")
{
    Playing p;
    p.ws.resize(150 * kCell.width, 24 * kCell.height, kCell);
    const auto bottom = [&]
    {
        const auto frame = p.ws.frame();
        const auto row = std::ranges::find(frame, Role::MessageLine, &Region::background);
        REQUIRE(row != frame.end());
        CHECK(row->bounds.y + row->bounds.height == frame.back().bounds.y); // just above it
        std::u32string text;
        for (const auto& region : frame)
            if (region.bounds.y == row->bounds.y)
                for (const auto& run : region.text)
                    text += run.text;
        std::vector<std::pair<int, std::u32string>> status;
        for (const auto& run : frame.back().text)
            // Leaving out the mode, and the padding after it that keeps the rest in place.
            if (run.text != U" NORMAL " && run.text != U" EDIT " && run.text != U" VISUAL " &&
                run.text.find_first_not_of(U' ') != std::u32string::npos)
                status.emplace_back(run.x, run.text);
        return std::pair{text, status};
    };
    auto [empty, status] = bottom();
    CHECK(empty.empty());
    // A long message, a hint while a field is edited, a Visual selection and the prompt each go
    // on the row, the status line under it as it was.
    p.ws.setMessage(
        "A message long enough to have pushed the indicators about when it shared their "
        "line",
        MessageKind::Information);
    CHECK(bottom().first.starts_with(U"A message long enough"));
    CHECK(bottom().second == status);
    p.press(Key::of(NamedKey::Escape));
    while (p.view.list().rows()[p.view.row()].kind != RowKind::Event)
        p.type(U"j");
    status = bottom().second; // the cursor's place, on the left, has moved
    p.press(Key::of(NamedKey::Enter));
    REQUIRE(p.view.typing());
    CHECK_FALSE(bottom().first.empty());
    CHECK(bottom().second == status);
    p.press(Key::of(NamedKey::Escape));
    p.type(U"Vj");
    CHECK_FALSE(bottom().first.empty());
    CHECK(bottom().second == status);
    p.press(Key::of(NamedKey::Escape));
    p.type(U":countin");
    CHECK(bottom().first.starts_with(U":countin"));
    CHECK(bottom().second == status);
    p.press(Key::of(NamedKey::Escape));
    CHECK(bottom().first.empty());
}
