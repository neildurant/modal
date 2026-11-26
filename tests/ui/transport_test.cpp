// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"
#include "support/temp_directory.hpp"

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/file_commands.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
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
    explicit Playing(core::Song song = demoSong())
        : document(std::move(song))
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
    core::Document document;
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

TEST_CASE("loop and punch have leader groups of their own, built alike, in Normal and Visual mode")
{
    Playing p;
    p.ws.setHints(true, 0);
    const auto hint = [&](std::string_view keys) -> std::optional<Workspace::Hint>
    {
        for (const auto& shown : p.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (shown.keys == keys)
            {
                return shown;
            }
        }
        return std::nullopt;
    };
    p.type(U"ga");
    for (const auto* start : {U",", U"v,"})
    {
        p.type(start);
        CHECK(hint("L") == Workspace::Hint{"L", "Loop", true});
        CHECK(hint("P") == Workspace::Hint{"P", "Punch", true});
        p.type(U"P");
        CHECK(hint("P"));
        CHECK(hint("l"));
        CHECK(hint("d"));
        p.press(Key::of(NamedKey::Escape));
        p.press(Key::of(NamedKey::Escape));
        p.type(start);
        p.type(U"L");
        CHECK(hint("L"));
        CHECK(hint("l"));
        p.press(Key::of(NamedKey::Escape));
        p.press(Key::of(NamedKey::Escape));
        // Recording keeps the tracks and the mode; labels no longer loop.
        p.type(start);
        p.type(U"r");
        CHECK(hint("a"));
        CHECK_FALSE(hint("p"));
        CHECK_FALSE(hint("l"));
        CHECK_FALSE(hint("P"));
        p.press(Key::of(NamedKey::Escape));
        p.press(Key::of(NamedKey::Escape));
        p.type(start);
        p.type(U"l");
        CHECK_FALSE(hint("L"));
        p.press(Key::of(NamedKey::Escape));
        p.press(Key::of(NamedKey::Escape));
    }
}

namespace
{
core::Song punchSong(core::Ppqn ppqn); // three sections, A, B and C, and labels in and out
} // namespace

TEST_CASE("the loop keys arm a loop from the section or the selection's time, and Space plays it")
{
    Playing p;
    CHECK(indicator(p, U"LOOP") == Shown::Off);
    p.type(U"ga,LL");
    CHECK_FALSE(p.player.playing()); // arming plays nothing
    CHECK(p.ws.message().starts_with("Loop range: "));
    CHECK(p.ws.message().ends_with("; Space plays it"));
    CHECK(indicator(p, U"LOOP") == Shown::Lit);
    REQUIRE(p.document.song().loop);
    CHECK(p.document.modified());

    p.type(U" ");
    REQUIRE(p.player.looping());
    CHECK(p.player.startPosition() == 0);
    CHECK(p.ws.message().starts_with("Looping "));
    p.runFor(50ms);
    p.type(U" ");
    CHECK_FALSE(p.player.looping());
    p.runFor(50ms);

    // A selection's time, replacing it; Space plays the new one.
    p.type(U"ggv,LL");
    CHECK(p.ws.mode() == Mode::Normal);
    CHECK(p.ws.message().find("1:1:000 to 2:1:000, replacing") != std::string::npos);
    p.type(U" ");
    REQUIRE(p.player.looping());
    CHECK(p.ws.message() == "Looping 1:1:000 to 2:1:000");

    // Armed while playing, ahead of the playhead, it loops at once.
    p.type(U"]s,LL");
    CHECK(p.ws.message().ends_with("; looping now"));
    REQUIRE(p.ws.loopSpan());
    CHECK(p.player.loopAsked() == std::pair{p.ws.loopSpan()->from, p.ws.loopSpan()->to});
}

TEST_CASE("a loop changed while playing takes effect at once: armed, switched, cleared and undone")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn); // sections A (bars 1-2), B (3-5) and C (6-)
    // Something at the end, so playback goes on that far.
    song.updateEvents(song.tracks()[0]->id(), [&](core::EventList& events)
                      { events.insert({7 * ppqn.whole(), core::Note{60, 90, ppqn.quarter()}}); });
    Playing p{std::move(song)};
    const core::Tick bar = ppqn.whole();
    const auto& tempo = p.document.song().tempo();
    p.type(U"ga ");
    REQUIRE(p.player.playing());
    CHECK_FALSE(p.player.looping());

    // Armed ahead of the playhead: on into it, then round it.
    p.type(U"]s,LL");
    CHECK(p.ws.message().ends_with("; looping now"));
    CHECK(p.player.looping());
    CHECK(contains(p.status(), U"↩ LOOP"));
    p.runFor(tempo.toTime(5 * bar) + 200ms);
    CHECK(p.player.position().value() >= 2 * bar);
    CHECK(p.player.position().value() < 3 * bar);

    // Switched off: playing on from where it is; on again, looping again.
    p.type(U",Lt");
    CHECK(p.ws.message() == "Looping off, the loop kept: playing on");
    CHECK_FALSE(p.player.looping());
    CHECK(p.player.playing());
    p.type(U",Lt");
    CHECK(p.ws.message() == "Looping on: looping now");
    CHECK(p.player.looping());

    // Undone and redone, and cleared.
    p.type(U"u");
    CHECK_FALSE(p.player.looping());
    p.press(Key::typed(U'r', kCtrl));
    CHECK(p.player.looping());
    p.type(U",Ld");
    CHECK_FALSE(p.player.looping());
    CHECK(p.player.playing());

    // Behind the playhead: the next Space plays it.
    p.runFor(tempo.toTime(6 * bar) - tempo.toTime(3 * bar));
    REQUIRE(p.player.position().value() >= 5 * bar);
    p.type(U"gg]s,LL"); // B, behind
    CHECK(p.ws.message().ends_with("; the next Space plays it"));
    CHECK_FALSE(p.player.looping());
    p.type(U" ");
    p.runFor(10ms);
}

TEST_CASE("choosing loop labels remembers both endpoints and loops across sections")
{
    auto song = demoSong();
    const auto join = song.sectionStart(1);
    const auto q = song.ppqn().quarter();
    song.setLabel(join - 2 * q, "Earlier");
    song.setLabel(join - q, "Before join");
    song.setLabel(join + q, "After join");
    Playing p{std::move(song)};
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    p.type(U"ga,LlBefore join");
    p.press(Key::of(NamedKey::Enter));
    p.type(U"After join");
    p.press(Key::of(NamedKey::Enter));
    CHECK_FALSE(p.player.playing());
    const auto range = p.ws.message();
    REQUIRE(range.starts_with("Loop range: Before join to After join, "));
    p.type(U" ");
    REQUIRE(p.player.looping());
    CHECK(p.player.startPosition() == join - q);
    const auto duration =
        p.document.song().tempo().toTime(join + q) - p.document.song().tempo().toTime(join - q);
    p.runFor(duration + 50ms);
    CHECK(p.player.position().value() >= join - q);
    CHECK(p.player.position().value() < join);

    // The same pair offered again.
    p.type(U",Ll");
    p.press(Key::of(NamedKey::Enter));
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message().starts_with(range.substr(0, range.find(';'))));

    p.type(U":loop \"After join\" \"Before join\"");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message() == "The end of a loop must be after its start");
    CHECK(p.player.looping());
}

TEST_CASE("Ctrl-Space plays from the cursor into an armed loop, within it, or past it straight on")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)}; // sections A (bars 1-2), B (3-5) and C (6-)
    const core::Tick bar = ppqn.whole();
    p.type(U"ga]s,LL"); // B
    REQUIRE(p.ws.loopSpan() == core::ArmedSpan{2 * bar, 5 * bar});

    const auto from = [&](std::u32string_view keys)
    {
        p.type(keys);
        p.press(Key::typed(U' ', kCtrl));
        REQUIRE(p.player.playing());
        const auto start = p.player.startPosition();
        const bool looping = p.player.looping();
        p.runFor(10ms);
        p.type(U" ");
        p.runFor(10ms);
        return std::pair{start, looping};
    };
    CHECK(from(U"gg") == std::pair{core::Tick{0}, true}); // before it, playing into it
    CHECK(from(U"gg]s") == std::pair{2 * bar, true});     // its start
    CHECK(from(U"gg]sl") == std::pair{3 * bar, true});    // within it
    CHECK(from(U"gg]s]s") == std::pair{5 * bar, false});  // past it, straight on

    // From within it, on to its end, then from its start.
    p.type(U"gg]sl");
    p.press(Key::typed(U' ', kCtrl));
    const auto& tempo = p.document.song().tempo();
    p.runFor(tempo.toTime(5 * bar) - tempo.toTime(3 * bar) + 100ms);
    CHECK(p.player.position().value() >= 2 * bar);
    CHECK(p.player.position().value() < 3 * bar);
    p.type(U" ");
    p.runFor(10ms);

    // From before it, it wraps at its end to its start.
    p.type(U"gg");
    p.press(Key::typed(U' ', kCtrl));
    p.runFor(tempo.toTime(5 * bar) + 100ms);
    CHECK(p.player.position().value() >= 2 * bar);
    CHECK(p.player.position().value() < 3 * bar);
}

TEST_CASE("looping switches off and on with , L t, keeping the loop; , L d clears it; each undone")
{
    Playing p{punchSong(core::kPpqn)};
    const core::Tick bar = core::kPpqn.whole();
    p.type(U"ga,Lt");
    CHECK(p.ws.message().starts_with("No loop is armed"));
    p.type(U"]s,LL");
    p.type(U",Lt");
    CHECK(p.ws.message() == "Looping off, the loop kept: Space plays from the marker");
    CHECK_FALSE(p.ws.loopSpan());
    CHECK(p.ws.armedRanges().loop == core::ArmedSpan{2 * bar, 5 * bar}); // still shown, dim
    CHECK_FALSE(p.ws.armedRanges().loopOn);
    CHECK(indicator(p, U"LOOP") == Shown::Off);
    p.type(U" ");
    CHECK(p.player.playing());
    CHECK_FALSE(p.player.looping());
    p.runFor(10ms);
    p.type(U" ");
    p.runFor(10ms);

    p.type(U",Lt");
    CHECK(p.ws.message() == "Looping on: Space plays the loop");
    CHECK(indicator(p, U"LOOP") == Shown::Lit);
    p.type(U"u");
    CHECK_FALSE(p.ws.loopSpan());
    p.press(Key::typed(U'r', kCtrl));
    CHECK(p.ws.loopSpan());

    p.type(U",Ld");
    CHECK(p.ws.message().starts_with("Loop cleared: B, "));
    CHECK_FALSE(p.document.song().loop);
    CHECK_FALSE(p.ws.armedRanges().loop);
    p.type(U",Ld");
    CHECK(p.ws.message() == "No loop is armed");
    p.type(U"u");
    CHECK(p.ws.loopSpan() == core::ArmedSpan{2 * bar, 5 * bar});
}

TEST_CASE("an armed loop follows its section, goes with it, and is saved with the song, off too")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)};
    const core::Tick bar = ppqn.whole();
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    p.type(U"ga]s,LL,Lt"); // B, off
    const auto read = core::readSong(core::writeSong(p.document.song()), ppqn);
    REQUIRE(read.song);
    CHECK(read.song->loop);
    CHECK_FALSE(read.song->loopOn);
    CHECK(core::writeSong(*read.song).find("\nloop section=2 off\n") != std::string::npos);

    // Joined into A, it goes, saying so; undoing the join brings it back.
    p.type(U",Lt");
    p.type(U"gg"); // A, joined with B after it
    p.type(U":section-join");
    p.press(Key::of(NamedKey::Enter));
    CHECK_FALSE(p.document.song().loop);
    CHECK(p.ws.message().ends_with("Loop cleared: its section is gone"));
    p.type(U"u");
    CHECK(p.ws.loopSpan() == core::ArmedSpan{2 * bar, 5 * bar});
}

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

    p.type(U" ,tk");
    REQUIRE(p.player.playing());
    p.press(Key::typed(U'.', kCtrl));
    CHECK_FALSE(p.player.playing());
    p.press(Key::of(NamedKey::Escape));

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
        const auto run =
            std::ranges::find_if(text, [&](const TextRun& candidate)
                                 { return candidate.text.find(start) != std::u32string::npos; });
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
        const auto run =
            std::ranges::find_if(text, [](const TextRun& candidate)
                                 { return candidate.text.find(U"■") != std::u32string::npos; });
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

TEST_CASE("input activity lights IN in every view, the light alone, and the light expires")
{
    Playing p;
    p.ws.resize(220 * kCell.width, 24 * kCell.height, kCell);
    const std::array<std::uint8_t, 3> on{0x90, 60, 96};
    REQUIRE(p.engine.receive(core::InputId{0}, on, p.clock.time));
    p.engine.step();
    CHECK(p.ws.inputWoken(p.now));
    const auto light = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text == U" IN ")
                return run.style.background;
        return Role::Warning;
    };
    CHECK(light() == Role::IndicatorInput);
    // The last message is the Ports view's, for each input (decided 25 November 2026).
    CHECK_FALSE(contains(p.status(), U"C4"));
    for (const auto view : {ViewKind::Tracks, ViewKind::PianoRoll, ViewKind::Arrangement})
    {
        p.ws.show(view);
        CHECK(light() == Role::IndicatorInput);
    }
    // The short collection deadline precedes the light's deadline.
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    REQUIRE(p.ws.deadline());
    p.ws.wake(*p.ws.deadline());
    CHECK(light() == Role::Indicator);
    CHECK_FALSE(p.ws.deadline());
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

class RecordingFiles final : public FileHost
{
public:
    explicit RecordingFiles(Playing* playing = nullptr)
        : playing_(playing)
    {
    }
    void chooseOpen(Chosen done) override { pending = std::move(done); }
    void chooseSave(FileKind, const std::filesystem::path&, Chosen done) override
    {
        pending = std::move(done);
    }
    void quit() override { quitted = true; }
    bool stopSongPlayback() override { return !playing_ || playing_->player.stop(); }
    void songReplaced() override
    {
        replaced = true;
        if (playing_)
            playing_->ws.showSong();
    }
    std::shared_ptr<void> leaveSong() override
    {
        return playing_ ? playing_->ws.leaveSong() : nullptr;
    }
    void returnToSong(std::shared_ptr<void> kept) override
    {
        if (kept)
            playing_->ws.returnToSong(std::move(kept));
        else
            songReplaced();
    }
    void report(const CommandResult& result) override { reported = result; }

    Chosen pending;
    bool quitted = false;
    bool replaced = false;
    std::optional<CommandResult> reported;

private:
    Playing* playing_;
};
} // namespace

TEST_CASE("changing songs stops playback without carrying the old play position over")
{
    Playing p;
    RecordingFiles files{&p};
    addFileCommands(p.registry, p.ws.recordingSession(), files);
    p.document.setListener([&](const auto& song) { p.player.update(song); });
    p.ws.setRewind(false);
    p.player.setMarker(p.document.song().ppqn().quarter());
    REQUIRE(p.player.play(p.document.song()));
    p.runFor(100ms);
    REQUIRE(p.player.playing());

    CHECK(p.registry.runLine("new").changed);
    CHECK(files.replaced);
    CHECK_FALSE(p.player.playing());
    CHECK(p.player.marker() == 0);
    p.runFor(100ms);
    CHECK(p.player.marker() == 0);
}

TEST_CASE("each open song keeps its own play-from marker")
{
    Playing p;
    RecordingFiles files{&p};
    addFileCommands(p.registry, p.ws.recordingSession(), files);
    p.document.setListener([&](const auto& song) { p.player.update(song); });
    const auto quarter = p.document.song().ppqn().quarter();
    p.player.setMarker(quarter);
    CHECK(p.registry.runLine("new").changed);
    CHECK(p.player.marker() == 0);
    p.player.setMarker(2 * quarter);
    CHECK(p.registry.runLine("song-previous").changed);
    CHECK(p.player.marker() == quarter);
    CHECK(p.registry.runLine("song-next").changed);
    CHECK(p.player.marker() == 2 * quarter);
}

TEST_CASE("file operations refuse a pending recording take, even after stop and with bang")
{
    Playing p;
    RecordingFiles files;
    test::TempDirectory directory{"modal-recording-files-test"};
    addFileCommands(p.registry, p.ws.recordingSession(), files, directory.path());
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    p.document.markSaved();
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    REQUIRE(hasPitch(p.document.song(), id, 99));
    REQUIRE_FALSE(p.document.modified());

    const auto check = [&](std::string_view refusal)
    {
        const auto before = p.document.underlying();
        for (const auto command :
             {U"q", U"q!", U"w", U"w!", U"wq", U"export", U"export!", U"e", U"e!", U"new", U"new!",
              U"song-select", U"song-close", U"song-close!"})
        {
            p.type(U":");
            p.type(command);
            p.press(Key::of(NamedKey::Enter));
            CHECK(p.ws.message() == refusal);
            CHECK(p.document.underlying() == before);
            CHECK_FALSE(files.quitted);
            CHECK_FALSE(files.replaced);
            CHECK_FALSE(files.pending);
        }
    };
    check("Stop recording first");
    p.type(U" "); // the engine has not acknowledged the stop yet
    check("Finishing the take: try again in a moment");
    p.runFor(10ms);
    REQUIRE(hasPitch(p.document.underlying(), id, 99));
    p.type(U":w take");
    p.press(Key::of(NamedKey::Enter));
    const auto saved = core::readSongFile(directory.path() / "take.modal");
    REQUIRE(saved.song);
    CHECK(hasPitch(*saved.song, id, 99));
    CHECK_FALSE(p.document.modified());
}

TEST_CASE("a file dialog answered during recording cannot change or save the song")
{
    Playing p;
    RecordingFiles files;
    test::TempDirectory directory{"modal-recording-dialog-test"};
    addFileCommands(p.registry, p.ws.recordingSession(), files, directory.path());
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    p.type(U",ra");
    p.document.markSaved();
    p.type(U":");
    p.type(GENERATE(U"e", U"w", U"wq", U"export"));
    p.press(Key::of(NamedKey::Enter));
    REQUIRE(files.pending);
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    const auto before = p.document.underlying();
    const auto path = directory.path() / "dialog.modal";
    auto existing = newSong();
    existing.title = "Existing file";
    REQUIRE(core::writeSongFile(existing, path).empty());
    const auto callback = std::move(files.pending);
    callback(path);
    REQUIRE(files.reported);
    CHECK(*files.reported == CommandResult::error("Stop recording first"));
    CHECK(p.document.underlying() == before);
    CHECK_FALSE(files.quitted);
    CHECK_FALSE(files.replaced);
    const auto saved = core::readSongFile(path);
    REQUIRE(saved.song);
    CHECK(saved.song->title == "Existing file");
}

TEST_CASE("an open picker keeps the finalised take in its song unless replacement was requested")
{
    Playing p;
    RecordingFiles files;
    test::TempDirectory directory{"modal-recorded-open-dialog-test"};
    addFileCommands(p.registry, p.ws.recordingSession(), files, directory.path());
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    p.type(U",ra");
    p.document.markSaved();
    const bool force = GENERATE(false, true);
    p.type(force ? U":e!" : U":e");
    p.press(Key::of(NamedKey::Enter));
    REQUIRE(files.pending);
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE(p.document.modified());
    const auto before = p.document.underlying();
    const auto path = directory.path() / "other.modal";
    REQUIRE(core::writeSongFile(newSong(), path).empty());
    const auto callback = std::move(files.pending);
    callback(path);
    REQUIRE(files.reported);
    CHECK_FALSE(files.reported->refused);
    CHECK(files.replaced);
    CHECK_FALSE(p.document.modified());
    REQUIRE(p.registry.runLine("song-close").changed);
    if (!force)
    {
        CHECK(p.document.underlying() == before);
        CHECK(p.document.modified());
    }
    else
        CHECK_FALSE(p.document.modified());
}

TEST_CASE("a recording session remains bound and protected while its stop is pending")
{
    Playing p;
    p.type(U",raR");
    p.runFor(2100ms);
    playNote(p, 99);
    const auto id = armed(p.document)->id();
    p.type(U" ");
    REQUIRE(p.ws.recordingSession().pending());
    REQUIRE_FALSE(p.player.playing());
    // A second Space before acknowledgement must not start another play over the take.
    p.type(U" ");
    CHECK_FALSE(p.player.playing());
    p.ws.setPlayer(nullptr);
    CHECK(p.ws.message() == "Finishing the take: try again in a moment");
    p.runFor(10ms);
    CHECK_FALSE(p.ws.recordingSession().pending());
    CHECK(hasPitch(p.document.underlying(), id, 99));
    p.ws.setPlayer(nullptr);
    p.type(U" ");
    CHECK(p.ws.message() == "Nothing to play to");
}

TEST_CASE("playing a section during a take respects the recording session")
{
    Playing p;
    p.type(U",raR");
    p.runFor(2100ms);
    playNote(p, 99);
    const auto id = armed(p.document)->id();
    const auto marker = p.player.marker();
    p.ws.show(ViewKind::Arrangement);
    p.type(U"k"); // the section strip
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message() == "Recording: Space stops it first");
    CHECK(p.player.marker() == marker);
    CHECK(p.player.recording());
    p.type(U" ");
    p.runFor(10ms);
    CHECK(hasPitch(p.document.underlying(), id, 99));
}

TEST_CASE("an event existing only in the live take is protected before a view edits its indices")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    p.document.edit("Empty recorded track", {}, [&](core::Song& song)
                    { song.updateEvents(id, [](core::EventList& events) { events = {}; }); });
    p.ws.refresh();
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    REQUIRE(p.document.underlying().events(id).empty());
    REQUIRE(hasPitch(p.document.song(), id, 99));
    const auto tick = p.document.song().events(id)[0].tick;
    p.view.setCursor({id, tick, std::uint8_t{99}});
    const auto kind = GENERATE(ViewKind::EventList, ViewKind::PianoRoll);
    p.ws.show(kind);
    const auto command = GENERATE(U"x", U">ib");
    p.type(command);
    CHECK(p.ws.message() ==
          armed(p.document)->name + " is being recorded: change it once recording stops");
    CHECK(p.document.underlying().events(id).empty());
    CHECK(hasPitch(p.document.song(), id, 99));
    p.type(U" ");
    p.runFor(10ms);
    CHECK(hasPitch(p.document.underlying(), id, 99));
}

namespace
{
// The machine's ports for recording: one input, Keys, plugged in, enabled and the default.
PortTable keysTable()
{
    PortTable table;
    table.detected(PortDirection::Input, std::vector<std::string>{"Keys"});
    table.setEnabled(PortDirection::Input, "Keys", true);
    table.setDefault(PortDirection::Input, "Keys");
    return table;
}

void setTrack(Playing& p, std::size_t index, bool arm, std::string input)
{
    const auto id = p.document.song().tracks()[index]->id();
    p.document.edit("Track", {},
                    [&](core::Song& song)
                    {
                        song.updateTrack(id,
                                         [&](core::Track& track)
                                         {
                                             track.arm = arm;
                                             track.input = input;
                                         });
                    });
}
} // namespace

TEST_CASE("an armed track whose input can't be used records nothing and is named when "
          "recording starts, and R refuses when no armed track can record")
{
    auto table = keysTable();
    Playing p;
    p.ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    const auto& tracks = p.document.song().tracks();
    const auto first = tracks[0]->name;
    const auto second = tracks[1]->name;

    // ARM stays red: the warning is said when R is pressed (decided after use).
    setTrack(p, 0, true, "Gone");
    CHECK(indicator(p, U"ARM 1") == Shown::Lit);
    p.type(U"R");
    CHECK_FALSE(p.player.recording());
    CHECK(p.ws.message() == "Nothing to record from: " + first + ", input Gone not found");
    CHECK(p.ws.messageKind() == MessageKind::Warning);

    setTrack(p, 1, true, "Elsewhere");
    p.type(U"R");
    CHECK_FALSE(p.player.recording());
    CHECK(p.ws.message() == "No armed track can record: " + first + ", input Gone not found; " +
                                second + ", input Elsewhere not found");

    // One that can, the other recording nothing and said to.
    setTrack(p, 0, true, "");
    CHECK(indicator(p, U"ARM 2") == Shown::Lit);
    p.type(U"R");
    CHECK(p.player.recording());
    CHECK(p.ws.message() == "Recording onto " + first + "; " + second +
                                " records nothing: input Elsewhere not found");
    CHECK(p.ws.messageKind() == MessageKind::Warning);
    p.type(U" ");
    p.runFor(10ms);

    setTrack(p, 1, false, "Elsewhere");
    CHECK(indicator(p, U"ARM 1") == Shown::Lit);
}

TEST_CASE("input lost or an input gone during a take is said as recording stops, and IN is in "
          "the warning colour from the loss until then")
{
    auto table = keysTable();
    Playing p;
    p.ws.setPortControl({.table = [&] { return &table; }, .change = {}, .look = {}});
    setTrack(p, 0, true, "");
    const auto name = p.document.song().tracks()[0]->name;

    SECTION("lost for want of taking it")
    {
        p.type(U"R");
        p.runFor(2100ms);
        playNote(p, 99);
        CHECK(indicator(p, U" IN ") != Shown::Warning);
        const std::array<std::uint8_t, 3> cc{0xB0, 1, 64};
        for (std::size_t i = 0; i < core::Engine::kMaxStamped + 100; ++i)
        {
            REQUIRE(p.engine.receive(core::InputId{0}, cc, p.clock.time));
            if (i % core::Engine::kInputBatch == core::Engine::kInputBatch - 1)
                test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
        }
        test::runUntil(p.engine, p.clock, p.clock.time + 1ms);
        const auto lost = p.player.inputLost();
        REQUIRE(lost > 0);
        CHECK(indicator(p, U" IN ") == Shown::Warning);
        p.type(U" ");
        p.runFor(10ms);
        REQUIRE_FALSE(p.player.recording());
        CHECK(p.ws.message() == "Recorded onto " + name + "; " + std::to_string(lost) +
                                    " MIDI input messages lost during the take, arriving faster "
                                    "than taken");
        CHECK(p.ws.messageKind() == MessageKind::Warning);
        CHECK(indicator(p, U" IN ") != Shown::Warning);
    }
    SECTION("an input unplugged")
    {
        p.type(U"R");
        p.runFor(2100ms);
        playNote(p, 99);
        table.detected(PortDirection::Input, std::vector<std::string>{});
        p.type(U" ");
        p.runFor(10ms);
        REQUIRE_FALSE(p.player.recording());
        CHECK(p.ws.message() == "Recorded onto " + name + "; " + name +
                                    " lost its input during the take: default input Keys not "
                                    "found");
        CHECK(p.ws.messageKind() == MessageKind::Warning);
    }
}

TEST_CASE("R records onto the armed track after a count-in, Space stops, and it's one undo step")
{
    Playing p;
    p.type(U"R");
    CHECK(p.ws.message() == "No track is armed: A arms a track");
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

namespace
{
// Where the note of that pitch starts on the track, if there is one.
std::optional<core::Tick> noteAt(const core::Song& song, core::TrackId id, std::uint8_t pitch)
{
    for (const auto& event : song.events(id).all())
        if (const auto* note = std::get_if<core::Note>(&event.data); note && note->pitch == pitch)
            return event.tick;
    return std::nullopt;
}

// Records from the marker with no count-in: R, then a note held for 200 ms after `after`, then
// a stop.
void takeWithout(Playing& p, std::chrono::nanoseconds after, std::uint8_t pitch)
{
    p.type(U"R");
    REQUIRE(p.player.recording());
    p.runFor(after);
    playNote(p, pitch);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE_FALSE(p.player.recording());
}
} // namespace

TEST_CASE("a take lands at the right ticks at a coarse resolution too")
{
    Playing p;
    // A song of one armed track at 96 to the quarter, at 120 BPM: a quarter is 500 ms.
    p.document.edit("Coarse", {},
                    [](core::Song& song)
                    {
                        song = core::Song{core::Ppqn{96}};
                        song.updateTempo([](core::TempoMap& tempo)
                                         { tempo.set(0, core::Tempo{500'000}); });
                        const auto id = song.addTrack();
                        song.updateTrack(id, [](core::Track& track) { track.arm = true; });
                    });
    p.player.update(p.document.song());
    p.registry.runLine("countin off");
    const auto id = p.document.song().tracks()[0]->id();
    takeWithout(p, 500ms, 60);
    const auto at = noteAt(p.document.song(), id, 60);
    REQUIRE(at);
    CHECK(*at >= 96);
    CHECK(*at <= 96 + 2); // within a few milliseconds of the beat
}

TEST_CASE("a take through a tempo change lands where the music is, not where the clock was")
{
    Playing p;
    const auto ppqn = p.document.song().ppqn();
    const auto q = ppqn.quarter();
    // 120 BPM for the first bar, then 60: bar 2's second beat is 2 s and then 1 s in.
    p.document.edit("Tempo", {},
                    [&](core::Song& song)
                    {
                        song.updateTempo(
                            [&](core::TempoMap& tempo)
                            {
                                tempo.set(0, core::Tempo{500'000});
                                tempo.set(ppqn.whole(), core::Tempo{1'000'000});
                            });
                    });
    p.player.update(p.document.song());
    p.registry.runLine("countin off");
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    takeWithout(p, 3s, 99);
    const auto at = noteAt(p.document.song(), id, 99);
    REQUIRE(at);
    CHECK(*at >= ppqn.whole() + q);
    CHECK(*at <= ppqn.whole() + q + q / 50); // within 20 ms at 60 BPM
}

TEST_CASE("after a take the marker goes back to the record point with rewind, and stays where "
          "it stopped without")
{
    Playing p;
    REQUIRE(p.ws.rewind());
    p.registry.runLine("countin off");
    p.type(U",ra");
    const auto from = p.player.marker();
    takeWithout(p, 1s, 99);
    CHECK(p.player.marker() == from);

    p.type(U",T ");
    REQUIRE_FALSE(p.ws.rewind());
    takeWithout(p, 1s, 98);
    CHECK(p.player.marker() > from);
    // The next take starts where the last one stopped.
    const auto next = p.player.marker();
    const auto id = armed(p.document)->id();
    takeWithout(p, 0ms, 97);
    const auto at = noteAt(p.document.song(), id, 97);
    REQUIRE(at);
    CHECK(*at >= next);
}

TEST_CASE("two takes in a row are an undo step each, the second replacing the first")
{
    Playing p;
    p.registry.runLine("countin off");
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    takeWithout(p, 500ms, 99);
    REQUIRE(hasPitch(p.document.song(), id, 99));
    // Over the same bars, so replace takes the first take out.
    takeWithout(p, 500ms, 98);
    CHECK(hasPitch(p.document.song(), id, 98));
    CHECK_FALSE(hasPitch(p.document.song(), id, 99));
    p.type(U"u");
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK_FALSE(hasPitch(p.document.song(), id, 98));
    p.type(U"u");
    CHECK_FALSE(hasPitch(p.document.song(), id, 99));
    CHECK_FALSE(hasPitch(p.document.song(), id, 98));
    // Overdubbing, a second take keeps the first.
    p.type(U",rm");
    takeWithout(p, 500ms, 99);
    takeWithout(p, 1s, 98);
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK(hasPitch(p.document.song(), id, 98));
    p.type(U"u");
    CHECK(hasPitch(p.document.song(), id, 99));
    CHECK_FALSE(hasPitch(p.document.song(), id, 98));
}

TEST_CASE("through a pre-roll, what's played before the record point is left out, a note held "
          "across it starts there, and the take follows")
{
    Playing p;
    const auto ppqn = p.document.song().ppqn();
    const auto q = ppqn.quarter();
    p.document.edit(
        "Tempo", {}, [](core::Song& song)
        { song.updateTempo([](core::TempoMap& tempo) { tempo.set(0, core::Tempo{500'000}); }); });
    p.registry.runLine("preroll 1");
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    p.player.setMarker(ppqn.whole()); // recording from bar 2, the song from bar 1
    p.type(U"R");
    REQUIRE(p.player.recording());
    const auto send = [&](std::array<std::uint8_t, 3> bytes)
    { REQUIRE(p.engine.receive(core::InputId{0}, bytes, p.clock.time)); };
    p.runFor(500ms);
    send({0x90, 50, 100}); // played and let go in the pre-roll
    p.runFor(200ms);
    send({0x80, 50, 0});
    p.runFor(800ms);
    send({0x90, 51, 100}); // held across the record point, 2 s in
    p.runFor(1000ms);
    send({0x80, 51, 0});
    p.runFor(500ms);
    send({0x90, 52, 100}); // 3 s in, two beats after the record point
    p.runFor(200ms);
    send({0x80, 52, 0});
    p.runFor(100ms);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE_FALSE(p.player.recording());
    const auto& song = p.document.song();
    CHECK_FALSE(noteAt(song, id, 50));
    CHECK(noteAt(song, id, 51) == ppqn.whole());
    const auto played = noteAt(song, id, 52);
    REQUIRE(played);
    CHECK(*played >= ppqn.whole() + 2 * q);
    CHECK(*played <= ppqn.whole() + 2 * q + q / 25); // within 20 ms
}

TEST_CASE("while overdubbing a pedal pressed in the count-in and held starts at the record point, "
          "and what was there stays")
{
    Playing p;
    p.type(U",ra,rm");
    REQUIRE(p.document.song().recordMode == core::RecordMode::Overdub);
    const auto id = armed(p.document)->id();
    const auto old = p.document.song().events(id).all();
    const std::vector<core::Event> was{old.begin(), old.end()};
    const auto from = p.player.marker();
    p.type(U"R");
    p.runFor(1500ms); // in the count-in
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0xB0, 64, 127},
                             p.clock.time));
    p.runFor(1000ms); // into the take
    playNote(p, 99);
    REQUIRE(
        p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0xB0, 64, 0}, p.clock.time));
    p.runFor(100ms);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE_FALSE(p.player.recording());
    const auto now = p.document.song().events(id).all();
    const std::vector<core::Event> after{now.begin(), now.end()};
    for (const auto& event : was)
        CHECK(std::ranges::find(after, event) != after.end());
    const auto down = std::ranges::find_if(
        after,
        [&](const core::Event& event)
        {
            const auto* cc = std::get_if<core::ControlChange>(&event.data);
            return cc && cc->controller == 64 && cc->value == 127 && event.tick == from;
        });
    CHECK(down != after.end());
    CHECK(hasPitch(p.document.song(), id, 99));
}

TEST_CASE("recording onto two armed tracks puts the take on each, and says so as a warning")
{
    Playing p;
    setTrack(p, 0, true, "");
    setTrack(p, 1, true, "");
    const auto& tracks = p.document.song().tracks();
    const auto first = tracks[0]->id();
    const auto second = tracks[1]->id();
    const auto names = tracks[0]->name + ", " + tracks[1]->name;
    p.registry.runLine("countin off");
    p.type(U"R");
    CHECK(p.ws.message() == "Recording onto " + names);
    CHECK(p.ws.messageKind() == MessageKind::Warning);
    p.runFor(500ms);
    playNote(p, 99);
    p.type(U" ");
    p.runFor(10ms);
    CHECK(hasPitch(p.document.song(), first, 99));
    CHECK(hasPitch(p.document.song(), second, 99));
    CHECK(p.ws.message() == "Recorded onto " + names);
    p.type(U"u");
    CHECK_FALSE(hasPitch(p.document.song(), first, 99));
    CHECK_FALSE(hasPitch(p.document.song(), second, 99));
}

TEST_CASE("a new or opened song plays from its start, the marker put back there")
{
    Playing p;
    p.player.setMarker(4 * p.document.song().ppqn().whole());
    p.document.replace(newSong(), std::nullopt);
    p.ws.showSong();
    CHECK(p.player.marker() == 0);
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

TEST_CASE("a note still held ends at the stop and recorded pedal state carries")
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
    CHECK_FALSE(has(p.document.song(), id, core::Event{end, core::ControlChange{64, 0}}));
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

TEST_CASE("a plan made during a take is of the song beneath, not of the take so far")
{
    Playing p;
    p.type(U",ra");
    const auto id = armed(p.document)->id();
    const auto channel = armed(p.document)->channel;
    // Quantised as it plays to whole bars, so a note played late in a bar plays at the next.
    p.document.edit("Quantise", {},
                    [&](core::Song& song)
                    {
                        song.recordMode = core::RecordMode::Overdub;
                        song.updateTrack(id,
                                         [](core::Track& track)
                                         {
                                             track.quantize.on = true;
                                             track.quantize.grid = core::NoteFraction{1, 1};
                                         });
                    });
    p.player.update(p.document.song());
    p.type(U"R");
    p.runFor(2100ms); // the count-in
    REQUIRE(p.player.recordPhase() == core::Player::RecordPhase::Recording);
    p.runFor(1300ms); // well into the second half of the bar
    const auto on = static_cast<std::uint8_t>(0x90 | (channel - 1));
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x90, 99, 100},
                             p.clock.time));
    p.runFor(50ms);
    REQUIRE(p.engine.receive(core::InputId{0}, std::array<std::uint8_t, 3>{0x80, 99, 64},
                             p.clock.time));
    p.runFor(50ms);
    REQUIRE(noteOn(p.document.song(), id, 99).has_value()); // in the take as shown
    const auto heard = [&]
    {
        return std::ranges::count_if(
            p.sink.sent, [&](const auto& sent)
            { return sent.bytes.size() == 3 && sent.bytes[0] == on && sent.bytes[1] == 99; });
    };
    const auto before = heard(); // through thru, if at all

    p.type(U",Tq"); // bypassed, and back: each plans the song again
    p.type(U",Tq");
    p.runFor(1500ms); // past the next bar line
    CHECK(heard() == before);
    p.type(U" ");
    p.runFor(10ms);
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
    // With nothing armed, the playback lead-in, here none (decided 25 November 2026).
    CHECK(indicator(p, U"NO COUNT") == Shown::Off);
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

TEST_CASE("the status line gives way in a narrow window, the least needed first, keeping the "
          "mode, the transport and the recording group")
{
    Playing p;
    p.document.edit("Rename section", {},
                    [](core::Song& song) { song.renameSection(0, "Introduction"); });
    // What gives way, in the order it goes.
    p.document.edit("Change", {}, [](core::Song& song) { song.title = "Changed"; }); // + lit
    const std::array<std::u32string_view, 10> parts{U" + ",   U"Introduction", U"REPLACE", U"THR",
                                                    U"Intro", U" IN ",         U" 4/4 ",   U" BPM",
                                                    U"PUNCH", U"LOOP"};
    const auto shown = [&](int columns)
    {
        p.ws.resize(columns * kCell.width, 24 * kCell.height, kCell);
        const auto frame = p.ws.frame();
        const auto& runs = frame.back().text;
        const auto has = [&](std::u32string_view part)
        {
            return std::ranges::any_of(runs, [&](const auto& run)
                                       { return run.text.find(part) != std::u32string::npos; });
        };
        for (const auto kept : {U" NORMAL ", U"■ STOP", U"1:1:000", U"ARM"})
        {
            CAPTURE(columns, encodeUtf8(kept));
            CHECK(has(kept));
        }
        std::array<bool, 10> present{};
        for (std::size_t i = 0; i < parts.size(); ++i)
            present[i] = has(parts[i]);
        return present;
    };
    CHECK(std::ranges::all_of(shown(220), std::identity{}));
    bool narrowest = false;
    for (int columns = 220; columns >= 61; --columns) // as narrow as the rest still fits
    {
        const auto present = shown(columns);
        CAPTURE(columns, present);
        // Gone in order: none still there after one that's gone.
        const auto first = std::ranges::find(present, true);
        CHECK(std::all_of(first, present.end(), std::identity{}));
        narrowest = narrowest || first == present.end();
    }
    CHECK(narrowest);
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

TEST_CASE("the status line's transport, meter, tempo and section are each on a panel, and "
          "recording is lit as arming is")
{
    Playing p;
    p.ws.resize(220 * kCell.width, 24 * kCell.height, kCell);
    const auto background = [&](std::u32string_view text)
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text.find(text) != std::u32string::npos)
                return run.style.background;
        return Role::Background;
    };
    CHECK(background(U"■") == Role::Indicator);
    CHECK(background(U"1:1:000") == Role::Indicator);
    CHECK(background(U" 4/4 ") == Role::Indicator);
    CHECK(background(U" BPM") == Role::Indicator);
    CHECK(background(decodeUtf8(p.document.song().sections()[0]->name())) == Role::Indicator);
    // Each a chip on the bar.
    const auto chips = p.ws.frame();
    for (const auto& run : chips.back().text)
        if (run.style.background != Role::Bar)
            CHECK((run.style.attributes & kInset) != 0);
    // The transport's state a block as wide stopped, playing or recording, so nothing moves.
    const auto meterX = [&]
    {
        const auto frame = p.ws.frame();
        for (const auto& run : frame.back().text)
            if (run.text == U" 4/4 ")
                return run.x;
        return -1;
    };
    const auto stopped = meterX();
    CHECK(background(U"■ STOP") == Role::Indicator);
    p.type(U",ra");
    p.type(U"R");
    p.runFor(10ms);
    REQUIRE(p.player.recordPhase() == core::Player::RecordPhase::CountIn);
    CHECK(background(U"● COUNT") == Role::IndicatorArm);
    CHECK(meterX() == stopped);
    p.runFor(2100ms);
    REQUIRE(p.player.recordPhase() == core::Player::RecordPhase::Recording);
    CHECK(background(U"● REC") == Role::IndicatorArm);
    CHECK(meterX() == stopped);
    p.type(U" ");
    p.runFor(10ms);
}

TEST_CASE("A arms the cursor's track, its button saying A, and R while it's recorded")
{
    Playing p;
    p.type(U"ga");
    p.ws.resize(132 * kCell.width, 30 * kCell.height, kCell);
    const auto badge = [&]
    {
        const auto frame = p.ws.frame();
        const auto& page = p.ws.arrangement().page();
        const int y = (ArrangePage::kRulerRows + static_cast<int>(page.track() - page.topTrack())) *
                      kCell.height;
        const int from = (4 + ArrangePage::kNameColumns) * kCell.width;
        for (const auto& region : frame)
        {
            for (const auto& run : region.text)
            {
                // The last of the buttons, Arm.
                if (run.y == y && run.x > from + 2 * 4 * kCell.width &&
                    run.x < page.headerColumns() * kCell.width && run.text.size() == 3)
                {
                    return std::pair{run.text, run.style.foreground};
                }
            }
        }
        return std::pair{std::u32string{}, Role::Background};
    };
    CHECK(badge() == std::pair{std::u32string{U" A "}, Role::Dim});
    p.type(U"A");
    const auto* track = armed(p.document);
    REQUIRE(track != nullptr);
    CHECK(p.ws.message().starts_with("Armed"));
    CHECK(badge() == std::pair{std::u32string{U" A "}, Role::ToggleText});
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(badge() == std::pair{std::u32string{U" R "}, Role::ToggleText});
    p.type(U" ");
    p.runFor(10ms);
    CHECK_FALSE(p.player.recording());
    CHECK(badge() == std::pair{std::u32string{U" A "}, Role::ToggleText});
    p.type(U"A");
    CHECK(armed(p.document) == nullptr);
    CHECK(badge() == std::pair{std::u32string{U" A "}, Role::Dim});
}

TEST_CASE("R while a stopped take is being finished says so, rather than asking to stop")
{
    Playing p;
    p.type(U",ra");
    p.type(U"R");
    p.runFor(2100ms);
    playNote(p, 99);
    p.type(U" "); // stopped, the engine not yet acknowledging it
    REQUIRE(p.ws.recordingSession().state() == core::RecordingSession::State::Finishing);
    p.type(U"R");
    CHECK(p.ws.message() == "Finishing the take: try again in a moment");
}

namespace
{

// Eight bars of 4/4 in three sections, A (bars 1-2), B (3-5) and C (6-8), with a track, a
// label "in" at bar 2 and "out" at bar 7.
core::Song punchSong(core::Ppqn ppqn)
{
    core::Song song{ppqn};
    const core::Tick bar = ppqn.whole();
    song.addTrack("Keys");
    song.renameSection(0, "A");
    song.splitSection(2 * bar, "B");
    song.splitSection(5 * bar, "C");
    song.setLabel(bar, "in");
    song.setLabel(6 * bar, "out");
    return song;
}

} // namespace

TEST_CASE("a punch range is armed from a section, a selection or two labels, until cleared")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)};
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };
    const auto armed = [&] { return p.ws.punchSpan(); };
    const auto shows = [&](std::u32string_view text)
    {
        return std::ranges::any_of(p.ws.frame(),
                                   [&](const Region& region)
                                   {
                                       return std::ranges::any_of(
                                           region.text, [&](const TextRun& run)
                                           { return contains(run.text, text); });
                                   });
    };

    CHECK(indicator(p, U"PUNCH") == Shown::Off);
    p.type(U"ga,PP");
    CHECK(p.ws.message() == "Punch range: A, " + at(0) + " to " + at(2 * bar));
    CHECK(armed() == core::ArmedSpan{0, 2 * bar});
    CHECK(indicator(p, U"PUNCH") == Shown::Lit);

    // Another section replaces it, saying what it replaced.
    p.type(U"]s,PP");
    CHECK(p.ws.message() == "Punch range: B, " + at(2 * bar) + " to " + at(5 * bar) +
                                ", replacing A, " + at(0) + " to " + at(2 * bar));
    CHECK(armed() == core::ArmedSpan{2 * bar, 5 * bar});

    // A Visual selection's time, ending the selection.
    p.type(U"ggvl,PP");
    CHECK(p.ws.mode() == Mode::Normal);
    CHECK(armed() == core::ArmedSpan{0, 2 * bar});
    CHECK(p.ws.message().starts_with("Punch range: " + at(0) + " to " + at(2 * bar) +
                                     ", replacing B, "));

    // Two labels, chosen, across a section join; the pair is offered again.
    p.type(U",Pl");
    CHECK(shows(U"Punch in: "));
    p.type(U"in");
    p.press(Key::of(NamedKey::Enter));
    CHECK(shows(U"Punch out: "));
    p.type(U"out");
    p.press(Key::of(NamedKey::Enter));
    CHECK(armed() == core::ArmedSpan{bar, 6 * bar});
    CHECK(p.ws.message().starts_with("Punch range: in to out, " + at(bar) + " to " + at(6 * bar) +
                                     ", replacing "));
    p.type(U",Pl");
    p.press(Key::of(NamedKey::Enter));
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message() == "Punch range: in to out, " + at(bar) + " to " + at(6 * bar));

    // By name at the prompt; the same range again replaces nothing.
    p.type(U":punch out in");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message() == "The end of a punch range must be after its start");
    p.type(U":punch nowhere out");
    p.press(Key::of(NamedKey::Enter));
    CHECK(p.ws.message() == "No label named nowhere");
    CHECK(armed() == core::ArmedSpan{bar, 6 * bar});

    // Arming plays nothing; clearing.
    CHECK_FALSE(p.player.playing());
    p.type(U",Pd");
    CHECK(p.ws.message() == "Punch range cleared: in to out, " + at(bar) + " to " + at(6 * bar));
    CHECK_FALSE(armed());
    CHECK(indicator(p, U"PUNCH") == Shown::Off);
    p.type(U",Pd");
    CHECK(p.ws.message() == "No punch range is armed");
    CHECK_FALSE(p.ws.refused()); // nothing to do, as disarming with no track armed
}

TEST_CASE("an armed punch range follows its section or labels through edits, and goes with them, "
          "undo bringing it back")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)};
    p.ws.setLineHandler([&](const std::string& line) { p.ws.report(p.registry.runLine(line)); });
    const core::Tick bar = ppqn.whole();
    const auto edit = [&](const std::function<void(core::Song&)>& change)
    {
        p.document.edit("Edit", {}, change);
        p.press(Key::of(NamedKey::Escape)); // a key, after which the range is looked at
    };

    p.type(U"ga]s,PP");                                       // B
    edit([](core::Song& song) { song.resizeSection(0, 1); }); // A a bar longer
    CHECK(p.ws.punchSpan() == core::ArmedSpan{3 * bar, 6 * bar});
    edit([](core::Song& song) { song.renameSection(1, "Verse"); });
    edit([](core::Song& song) { song.moveSection(1, 1); });
    CHECK(p.ws.punchSpan() == core::ArmedSpan{6 * bar, 9 * bar});
    CHECK(p.ws.message().empty());

    // Joined into the section before, it's gone, and says so after what the key did.
    p.type(U"gg]s");
    p.type(U":section-join");
    p.press(Key::of(NamedKey::Enter));
    CHECK_FALSE(p.ws.punchSpan());
    CHECK(p.ws.message().ends_with("Punch range cleared: its section is gone"));
    CHECK(p.ws.messageKind() == MessageKind::Warning);
    // Cleared in the join's own undo step, so undoing the join arms it again.
    p.type(U"u");
    CHECK(p.ws.punchSpan() == core::ArmedSpan{6 * bar, 9 * bar});
    p.press(Key::typed(U'r', kCtrl));
    CHECK_FALSE(p.ws.punchSpan());
    CHECK_FALSE(p.document.song().punch);

    // Labels: followed as they move with their section, and gone with one.
    p.type(U":punch in out");
    p.press(Key::of(NamedKey::Enter));
    const auto before = p.ws.punchSpan();
    REQUIRE(before);
    edit([](core::Song& song) { song.resizeSection(0, 1); });
    CHECK(p.ws.punchSpan() == core::ArmedSpan{before->from, before->to + bar});
    edit([&](core::Song& song) { song.removeLabel(before->from); });
    CHECK_FALSE(p.ws.punchSpan());
    CHECK_FALSE(p.document.song().punch); // gone from the song, in the edit's step
    p.type(U"u");
    CHECK(p.ws.punchSpan() == core::ArmedSpan{before->from, before->to + bar});

    // A selection's time stays where it is, cut short at the end.
    p.type(U"gg");
    p.type(U"Gv,PP");
    const auto span = p.ws.punchSpan();
    REQUIRE(span);
    edit([](core::Song& song) { song.insertBars(0, 1); });
    CHECK(p.ws.punchSpan() == span);
}

TEST_CASE("arming and clearing a punch range are undo steps, and the song is changed by them")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)};
    const core::Tick bar = ppqn.whole();
    REQUIRE_FALSE(p.document.modified());

    p.type(U"ga]s,PP"); // B
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
    CHECK(p.document.modified());
    p.type(U"u");
    CHECK_FALSE(p.ws.punchSpan());
    CHECK_FALSE(p.document.modified());
    CHECK(indicator(p, U"PUNCH") == Shown::Off);
    p.press(Key::typed(U'r', kCtrl));
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
    CHECK(indicator(p, U"PUNCH") == Shown::Lit);

    // Arming the same range again is no step; clearing is one.
    p.document.markSaved();
    p.type(U",PP");
    CHECK(p.ws.message().starts_with("Punch range: B"));
    CHECK_FALSE(p.document.modified());
    p.type(U",Pd");
    CHECK_FALSE(p.ws.punchSpan());
    CHECK(p.document.modified());
    p.type(U"u");
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
}

TEST_CASE("punching switches off and on with , P t, keeping the range; off, R records from the "
          "marker")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Playing p{punchSong(ppqn)};
    const core::Tick bar = ppqn.whole();
    p.type(U"ga,Pt");
    CHECK(p.ws.message().starts_with("No punch range is armed"));
    p.type(U"]s,PP"); // B
    p.type(U",Pt");
    CHECK(p.ws.message() == "Punching off, the range kept: R records from the marker");
    CHECK_FALSE(p.ws.punchSpan());
    CHECK(p.ws.armedRanges().punch == core::ArmedSpan{2 * bar, 5 * bar}); // still shown, dim
    CHECK_FALSE(p.ws.armedRanges().punchOn);
    CHECK(indicator(p, U"PUNCH") == Shown::Off);
    p.type(U"u");
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
    p.press(Key::typed(U'r', kCtrl));
    CHECK_FALSE(p.ws.punchSpan());

    // Off, R records from the marker, as with nothing armed.
    p.type(U"A");
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.ws.message().find("punching") == std::string::npos);
    p.type(U" ");
    p.runFor(10ms);

    // Arming again switches it on.
    p.type(U"gg]s,PP");
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
    CHECK(indicator(p, U"PUNCH") == Shown::Lit);
}

TEST_CASE("a punch range saved with a song is armed when it's read, and R records it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    song.punch = core::ArmedRange::section(song, 1); // B
    const auto read = core::readSong(core::writeSong(song), ppqn);
    REQUIRE(read.song);
    Playing p{*read.song};
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };

    CHECK(indicator(p, U"PUNCH") == Shown::Lit);
    CHECK(p.ws.punchSpan() == core::ArmedSpan{2 * bar, 5 * bar});
    p.type(U"gaA");
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.ws.message() ==
          "Recording onto Keys, punching in " + at(2 * bar) + " to " + at(5 * bar));
}

TEST_CASE("R records the armed punch range, playing on past its end with the take in")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    song.countIn.mode = core::CountInSettings::Mode::None;
    const auto keys = song.tracks()[0]->id();
    song.updateEvents(keys, [&](core::EventList& events)
                      { events.insert({3 * ppqn.whole(), core::Note{60, 90, ppqn.quarter()}}); });
    Playing p{std::move(song)};
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };

    p.type(U"gaA]s,PP"); // B, bars 3 to 5
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.ws.message() ==
          "Recording onto Keys, punching in " + at(2 * bar) + " to " + at(5 * bar));
    CHECK(p.player.position().value_or(2 * bar) >= 2 * bar);

    // Three bars at 120 BPM, then a little past punch-out.
    p.runFor(5900ms);
    CHECK(p.player.recording());
    CHECK(contains(p.status(), U"● REC"));
    p.runFor(200ms);
    CHECK_FALSE(p.player.recording());
    CHECK(p.player.playing());
    CHECK(contains(p.status(), U"▶"));
    CHECK_FALSE(contains(p.status(), U"● REC"));
    CHECK(p.ws.message() == "Recorded onto Keys in the punch range");
    CHECK(p.document.song().events(keys).empty()); // nothing played: the old note replaced
    CHECK(p.document.canUndo());
    CHECK(indicator(p, U"PUNCH") == Shown::Lit); // still armed for another take

    p.type(U" ");
    p.runFor(10ms);
    p.type(U"u");
    CHECK(p.document.song().events(keys).size() == 1);
}

TEST_CASE("R records an armed loop pass after pass, each an undo step as it wraps")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    song.countIn.mode = core::CountInSettings::Mode::None;
    const auto keys = song.tracks()[0]->id();
    Playing p{std::move(song)};
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };
    const auto pitches = [&]
    {
        std::vector<int> result;
        for (const auto& event : p.document.song().events(keys).all())
            result.push_back(std::get<core::Note>(event.data).pitch);
        return result;
    };

    // With a punch range on too, refused for now.
    p.type(U"gaA,LL]s,PP"); // the loop A, bars 1 and 2; the punch range B
    p.type(U"R");
    CHECK(p.ws.message() == "A loop and a punch range are both on: ,Lt or ,Pt switches one off");
    CHECK_FALSE(p.player.playing());
    p.type(U",Pt");

    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.player.looping());
    CHECK(p.ws.message() == "Recording onto Keys, looping " + at(0) + " to " + at(2 * bar));
    p.runFor(1s);
    CHECK(contains(p.status(), U"● REC 1"));
    playNote(p, 60);

    // Two bars at 120 BPM: the first pass is in as it wraps, recording going on.
    p.runFor(3s);
    CHECK(p.player.recording());
    CHECK(contains(p.status(), U"● REC 2"));
    CHECK(p.ws.message() == "Recorded pass 1 onto Keys");
    CHECK(pitches() == std::vector{60});
    // The loop is kept to while recording it.
    p.type(U",Lt");
    CHECK(p.ws.message() == "Recording: Space stops it first");

    // The second pass, with nothing played, leaves the first; the third, stopped early,
    // replaces it as far as it got.
    p.runFor(4s);
    CHECK(contains(p.status(), U"● REC 3"));
    CHECK(pitches() == std::vector{60});
    p.runFor(1s);
    playNote(p, 64);
    p.type(U" ");
    p.runFor(10ms);
    CHECK_FALSE(p.player.playing());
    CHECK(p.ws.message() == "Recorded pass 3 onto Keys");
    CHECK(pitches() == std::vector{64});

    // Each pass its own undo step.
    p.type(U"u");
    CHECK(pitches() == std::vector{60});
    p.type(U"u");
    CHECK(pitches().empty());

    // Stopped in a later pass with nothing played in it, the passes before are kept.
    p.type(U"R");
    REQUIRE(p.player.recording());
    p.runFor(1s);
    playNote(p, 67);
    p.runFor(4s);
    p.type(U" ");
    p.runFor(10ms);
    CHECK(p.ws.message() == "Recording stopped: nothing was played in pass 2");
    CHECK(pitches() == std::vector{67});
}

TEST_CASE("R while a loop plays records it from its next pass, the tracks replaced silent at once")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    const auto keys = song.tracks()[0]->id();
    song.updateEvents(keys, [&](core::EventList& events)
                      { events.insert({ppqn.whole(), core::Note{48, 90, ppqn.quarter()}}); });
    Playing p{std::move(song)};
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };
    const auto pitches = [&]
    {
        std::vector<int> result;
        for (const auto& event : p.document.song().events(keys).all())
            result.push_back(std::get<core::Note>(event.data).pitch);
        return result;
    };
    const auto heard = [&](std::uint8_t pitch)
    {
        return std::ranges::any_of(p.sink.sent,
                                   [&](const auto& sent)
                                   {
                                       return sent.bytes.size() == 3 && sent.bytes[1] == pitch &&
                                              (sent.bytes[0] & 0xF0) == 0x90;
                                   });
    };

    p.type(U"gaA,LL "); // looping A, bars 1 and 2
    REQUIRE(p.player.looping());
    p.runFor(500ms);
    p.sink.sent.clear();
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.ws.message() == "Recording onto Keys, looping " + at(0) + " to " + at(2 * bar) +
                                ", from the loop's start");
    CHECK(contains(p.status(), U"● PRE"));
    p.runFor(2s);
    CHECK_FALSE(heard(48)); // silent at once, the old note in bar 2 not played
    p.runFor(2s);
    CHECK(contains(p.status(), U"● REC 1"));
    p.runFor(500ms);
    playNote(p, 60);
    p.runFor(4s);
    CHECK(p.ws.message() == "Recorded pass 1 onto Keys");
    CHECK(pitches() == std::vector{60});
    CHECK(contains(p.status(), U"● REC 2"));
    p.type(U" ");
    p.runFor(10ms);
    CHECK_FALSE(p.player.playing());

    // With a punch range on too, refused as from stopped.
    p.type(U"]s,PPgg");
    p.type(U" ");
    REQUIRE(p.player.looping());
    p.type(U"R");
    CHECK(p.ws.message() == "A loop and a punch range are both on: ,Lt or ,Pt switches one off");
    CHECK_FALSE(p.player.recording());
}

TEST_CASE("R while playing punches in ahead of the playhead, and is refused past punch-in")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    // Something at the end, so playback goes on that far.
    song.updateEvents(song.tracks()[0]->id(), [&](core::EventList& events)
                      { events.insert({7 * ppqn.whole(), core::Note{60, 90, ppqn.quarter()}}); });
    Playing p{std::move(song)};
    const core::Tick bar = ppqn.whole();
    const auto at = [&](core::Tick tick) { return positionText(p.document.song().meter(), tick); };

    // Playing without a range: refused as before.
    p.type(U"gaA");
    p.type(U" ");
    REQUIRE(p.player.playing());
    p.runFor(100ms);
    p.type(U"R");
    CHECK(p.ws.message() == "Stop first to record");
    p.type(U" ");
    p.runFor(10ms);

    p.type(U"]s,PP"); // B, bars 3 to 5
    p.type(U"gg ");   // playing from the start
    p.runFor(1s);
    p.type(U"R");
    REQUIRE(p.player.recording());
    CHECK(p.ws.message() ==
          "Recording onto Keys, punching in " + at(2 * bar) + " to " + at(5 * bar));
    CHECK(contains(p.status(), U"● PRE")); // before punch-in
    p.runFor(3500ms);                      // bar 3, at 4 s
    CHECK(contains(p.status(), U"● REC"));
    p.runFor(5400ms);
    CHECK(p.player.recording());
    p.runFor(200ms); // past punch-out, at 10 s
    CHECK_FALSE(p.player.recording());
    CHECK(p.player.playing());
    // An empty range, and nothing played in it.
    CHECK(p.ws.message() == "Nothing recorded: nothing was played");

    // Past punch-in now: refused.
    p.type(U"R");
    CHECK(p.ws.message() == "Past the punch-in point: stop first, then R");
    CHECK_FALSE(p.player.recording());
}

TEST_CASE("the take keys choose among a section's stack in order")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    song.countIn.mode = core::CountInSettings::Mode::None;
    const auto keys = song.tracks()[0]->id();
    const core::Tick bar = ppqn.whole();
    song.updateEvents(keys, [&](core::EventList& events)
                      { events.insert({0, core::Note{50, 100, bar / 4}}); }); // entered by hand
    Playing p{std::move(song)};
    const auto pitches = [&]
    {
        std::vector<int> result;
        for (const auto& event : p.document.song().events(keys).all())
            result.push_back(std::get<core::Note>(event.data).pitch);
        return result;
    };

    // Two passes of the loop A in replace, each a take; the original part is Take 1.
    p.type(U"gaA,LL");
    p.type(U"R");
    REQUIRE(p.player.recording());
    p.runFor(1s);
    playNote(p, 60);
    p.runFor(3s);
    playNote(p, 64);
    p.runFor(4s);
    p.type(U" ");
    p.runFor(10ms);
    REQUIRE_FALSE(p.player.playing());
    REQUIRE(pitches() == std::vector{64});

    p.type(U"[t");
    CHECK(p.ws.message() == "Take 2 of 3 on Keys in A, bars 1–2");
    CHECK(pitches() == std::vector{60});
    p.type(U"[t");
    CHECK(p.ws.message() == "Take 1 of 3 on Keys in A, bars 1–2");
    CHECK(pitches() == std::vector{50});
    p.type(U"[t"); // round to the last
    CHECK(p.ws.message() == "Take 3 of 3 on Keys in A, bars 1–2");
    CHECK(pitches() == std::vector{64});
    p.type(U"2]t"); // round past Take 1
    CHECK(p.ws.message() == "Take 2 of 3 on Keys in A, bars 1–2");
    CHECK(pitches() == std::vector{60});

    // Each choice an undo step.
    p.type(U"u");
    CHECK(pitches() == std::vector{64});

    // While playing, at once.
    p.type(U" ");
    REQUIRE(p.player.playing());
    p.type(U"]t");
    CHECK(p.player.playing());
    CHECK(pitches() == std::vector{50});
    p.type(U" ");
    p.runFor(10ms);

    const auto takeCount = [&]
    {
        const auto* part = p.document.song().sections()[0]->content(keys);
        return part ? part->takes.size() : 0;
    };
    // Discard the original part, so the newest performance plays in its place.
    p.type(U",kd");
    CHECK(p.ws.message() == "Discarded the take on Keys in A");
    CHECK(pitches() == std::vector{64});
    CHECK(takeCount() == 2);
    p.type(U"u");
    CHECK(pitches() == std::vector{50});
    CHECK(takeCount() == 3);

    p.type(U"]t,ko");
    CHECK(p.ws.message() == "Kept only this take on Keys in A");
    CHECK(pitches() == std::vector{60});
    REQUIRE(takeCount() == 1);
    p.type(U",ko");
    CHECK(p.ws.message() == "Only one take on Keys in A");
    p.type(U"u");
    CHECK(takeCount() == 3);
    CHECK(pitches() == std::vector{60});
    p.type(U",ko,kd");
    CHECK(p.ws.message() == "Discarded the take on Keys in A. No takes left");
    CHECK(takeCount() == 0);
    CHECK(pitches().empty());
    p.type(U"u");
    CHECK(takeCount() == 1);
    CHECK(pitches() == std::vector{60});

    // Another section, with none.
    p.type(U"]s]t");
    CHECK(p.ws.message() == "No takes on Keys in B");
    p.type(U",kd");
    CHECK(p.ws.message() == "No takes on Keys in B");
}

TEST_CASE("discarding takes repeatedly refreshes the event list before drawing")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    const auto keys = song.tracks()[0]->id();
    const auto bar = ppqn.whole();
    const auto beat = ppqn.quarter();
    const auto to = bar + 2 * beat;
    REQUIRE(song.updatePart(0, keys,
                            [&](core::TrackContent& part)
                            {
                                core::Take earlier{1, bar, to, 1, {}};
                                earlier.events.insert({bar, core::Note{71, 90, beat}});
                                core::Take later{2, bar, to, 2, {}};
                                later.events.insert({bar, core::Note{72, 90, beat}});
                                later.events.insert({bar + beat, core::Note{69, 90, beat}});
                                part.takes = {earlier, later};
                                part.events.insert({to, core::Note{60, 90, beat}});
                            }));
    Playing p{std::move(song)};
    p.type(U"galge");
    REQUIRE(p.view.cursor().tick == bar);
    REQUIRE(p.document.song().events(keys).size() == 3);

    const auto shows = [&](std::u32string_view text)
    {
        const auto frame = p.ws.frame();
        return std::ranges::any_of(frame.front().text,
                                   [&](const TextRun& run) { return contains(run.text, text); });
    };
    p.type(U"[t");
    CHECK(shows(U"B4"));
    CHECK_FALSE(shows(U"C5"));
    CHECK(p.view.cursor().tick == bar);
    p.type(U"u");

    p.type(U",kd");
    CHECK(shows(U"B4"));
    CHECK_FALSE(shows(U"C5"));
    CHECK(p.view.cursor().tick == bar);
    p.type(U",kd");
    CHECK(p.ws.message() == "Discarded the take on Keys in A. No takes left");
    CHECK_FALSE(shows(U"B4"));
    CHECK(shows(U"C4"));
    CHECK(p.view.cursor().tick == bar);

    const auto version = p.document.version();
    p.type(U",kd");
    CHECK(p.ws.message() == "No takes on Keys in A");
    CHECK(p.document.version() == version);
    p.type(U"u");
    CHECK(shows(U"B4"));
}

TEST_CASE("musical views show active takes, including a silent punch")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto song = punchSong(ppqn);
    const auto track = song.tracks()[0]->id();
    const auto bar = ppqn.whole();
    REQUIRE(song.updatePart(0, track,
                            [&](core::TrackContent& part)
                            {
                                core::Take first{1, 0, 2 * bar, 1, {}};
                                first.events.insert({0, core::Note{60, 90, ppqn.quarter()}});
                                part.takes = {first, {2, bar, 2 * bar, 2, {}}};
                            }));
    Playing p{std::move(song)};
    p.ws.resize(112 * kCell.width, 28 * kCell.height, kCell);
    const auto shows = [&](std::u32string_view text)
    {
        return std::ranges::any_of(p.ws.frame(),
                                   [&](const Region& region)
                                   {
                                       return std::ranges::any_of(
                                           region.text, [&](const TextRun& run)
                                           { return contains(run.text, text); });
                                   });
    };

    p.type(U"ga4zi");
    CHECK(shows(U"T1"));
    CHECK(shows(U"T2"));

    p.type(U"galgr");
    CHECK(shows(U"T1"));
    CHECK(shows(U"T2"));
    CHECK(shows(U"Editing:"));
    CHECK(shows(U"Take 2"));
    p.type(U",kd");
    CHECK(shows(U"Take 1"));
    CHECK_FALSE(shows(U"T2"));
    p.type(U"u");
    CHECK(shows(U"Take 2"));
    p.ws.resize(80 * kCell.width, 24 * kCell.height, kCell);
    CHECK(shows(U"Edit:"));
    CHECK(shows(U" T2"));

    p.type(U"gazozozozozozozo");
    CHECK_FALSE(shows(U"T2"));
    const auto frame = p.ws.frame();
    const auto& timeline = frame[1];
    CHECK(std::ranges::any_of(timeline.under, [](const Shape& shape)
                              { return shape.role == Role::Label && shape.alpha == 210; }));
}
