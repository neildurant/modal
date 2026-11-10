// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/core/pitch.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// A song of 8 bars in 4/4 and two tracks: a bass with a chord at the start (G1 and E1), an A2
// in bar 2, a C2 a little after bar 5's start with the sustain pedal down just after it, and
// an A1 in bar 8; and a lead with no notes.
core::Song bassAndLead(core::Ppqn ppqn)
{
    const auto q = ppqn.quarter();
    core::Song song{ppqn};
    const auto bass = song.addTrack("Bass");
    song.addTrack("Lead");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{31, 80, q / 2}});
                          events.insert({0, core::Note{28, 96, q / 2}});
                          events.insert({4 * q, core::Note{45, 50, q}});
                          events.insert({16 * q + q / 16, core::Note{36, 100, q / 4}});
                          events.insert({16 * q + q / 8, core::ControlChange{64, 127}});
                          events.insert({28 * q, core::Note{33, 70, 4 * q}});
                      });
    song.fitToContent();
    return song;
}

// The song in a workspace of 120 by 30 cells, in the arrangement.
struct Operating
{
    explicit Operating(core::Ppqn ppqn = core::kPpqn)
        : document{bassAndLead(ppqn)}
        , q{ppqn.quarter()}
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
    }

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    }
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }

    // The bass's notes, as start and pitch, and its pedals' times.
    std::vector<std::pair<core::Tick, int>> notes() const
    {
        std::vector<std::pair<core::Tick, int>> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                found.emplace_back(event.tick, note->pitch);
            }
        }
        return found;
    }
    std::vector<int> velocities() const
    {
        std::vector<int> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                found.push_back(note->velocity);
            }
        }
        return found;
    }
    std::vector<core::Tick> pedals() const
    {
        std::vector<core::Tick> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (std::holds_alternative<core::ControlChange>(event.data))
            {
                found.push_back(event.tick);
            }
        }
        return found;
    }

    core::Document document;
    core::Tick q;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

using Notes = std::vector<std::pair<core::Tick, int>>;

} // namespace

TEST_CASE("> and < transpose the notes a range or object takes, the count before saying how far")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr"); // the piano roll, at the start
    o.type(U">>");
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose 2 notes in Bass up a semitone");
    o.type(U"3<<");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 28});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose 2 notes in Bass down 3 semitones");

    // The count after the operator is the range's: two bars.
    o.type(U">2ib");
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 46}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"u");

    // Past the highest note: refused, naming the note.
    o.type(U"100>>");
    CHECK(o.ws.message().starts_with("The G1 at 1:1:0"));
    CHECK(o.ws.message().ends_with(" would go above G9, the highest note"));
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 31});

    // The operator and its count wait at the status line.
    o.type(U"3<");
    CHECK(o.ws.pending() == "3<");
    o.press(NamedKey::Escape);
}

TEST_CASE("t and T shift events later and earlier by the grid step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    o.type(U"tt"); // this bar's notes, a sixteenth later
    CHECK(o.notes().front() == std::pair{q / 4, 31});
    o.type(U"2TT");
    CHECK(o.ws.message().starts_with("The G1 at 1:1:"));
    CHECK(o.ws.message().ends_with(" would go before the song's start"));
    o.type(U"TT");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 31});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Shift 2 notes in Bass earlier by a grid step");

    // In the arrangement, controllers move with the notes; in the piano roll only notes move.
    o.type(U"ga4l"); // bar 5
    o.type(U"tt");
    CHECK(o.notes()[3].first == 16 * q + q / 16 + q / 4);
    CHECK(o.pedals() == std::vector{16 * q + q / 8 + q / 4});
    o.type(U"u");
    o.press(NamedKey::Enter); // bar 5 in the piano roll
    o.type(U"tt");
    CHECK(o.notes()[3].first == 16 * q + q / 16 + q / 4);
    CHECK(o.pedals() == std::vector{16 * q + q / 8});
}

TEST_CASE("q quantises notes' starts to the grid, and the event list and arrangement take rows "
          "and areas")
{
    Operating o;
    const auto q = o.q;
    // By default the pedalling goes with its notes, unasked; kept, it stays.
    o.type(U"4l"); // bar 5, in the arrangement, with the sustain pedal down after the C2
    o.type(U"qq");
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 16});
    CHECK(o.ws.message().empty());
    o.type(U"u");
    o.ws.setQuantizePedal(Workspace::QuantizePedal::Keep);
    o.type(U"qq");
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 8});
    o.type(U"u");
    // Or it asks.
    o.ws.setQuantizePedal(Workspace::QuantizePedal::Ask);
    o.type(U"qq");
    CHECK(o.ws.message() == "Move the pedalling with its notes? (y/n)");
    // Only y, n and Escape answer it; another key shows it again, doing nothing.
    o.type(U"jx");
    CHECK(o.ws.message() == "Move the pedalling with its notes? (y/n)");
    CHECK(o.notes()[3] == std::pair{16 * q + q / 16, 36});
    CHECK(o.ws.arrangement().page().bar() == 5);
    o.type(U"n"); // only the notes
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 8});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Quantise bar 5 of Bass");
    o.type(U"qqy"); // the pedal too, as far from the note as it was
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 16});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Quantise bar 5 of Bass, with its pedalling");
    o.type(U"qq");
    o.press(NamedKey::Escape);
    CHECK(o.ws.message() == "Nothing done");
    CHECK(o.notes()[3] == std::pair{16 * q + q / 16, 36});
    o.type(U"qqn");
    o.type(U"qqn");
    CHECK(o.ws.message() == "Nothing changed in bar 5 of Bass");
    o.type(U"u");

    // The event list's rows: this one and the next, from the first of the track's own (after
    // the conductor's).
    o.type(U"gegg");
    const auto& list = o.ws.eventList();
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        o.type(U"j");
    }
    o.type(U">j");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 29});
    CHECK(o.notes()[2] == std::pair{4 * q, 45});
    o.type(U"u");

    // Not on the conductor's list, nor on the section strip, nor over a single note in the
    // arrangement.
    o.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    o.type(U">>");
    CHECK(o.ws.message() == "The conductor's tempo and meter changes aren't changed this way");
    o.type(U"gags>>");
    CHECK(o.ws.message() ==
          "On the section strip, operators take is, this section's events on every track");
    o.type(U"j>in");
    CHECK(o.ws.message() == "There's no single note here; ib takes this bar's events");
}

TEST_CASE("after > the hint menu lists its ranges, but not ab, which is ib to it")
{
    Operating o;
    o.ws.setHints(true, 0);
    const auto title = [&](std::string_view keys) -> std::string
    {
        for (const auto& hint : o.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (hint.keys == keys)
            {
                return hint.title;
            }
        }
        return "(none)";
    };
    o.type(U"gr>");
    REQUIRE(o.ws.hints());
    CHECK(o.ws.hints()->front() == Workspace::Hint{">", "this bar's notes", false});
    CHECK(title("ib") == "this bar's notes");
    CHECK(title("ab") == "(none)");
    CHECK(title("]b") == "to the next bar line");
    o.press(NamedKey::Escape);
    o.type(U"<"); // shown as typed, not as <lt>
    CHECK(o.ws.hints()->front() == Workspace::Hint{"<", "this bar's notes", false});
    o.press(NamedKey::Escape);
    o.type(U"tab"); // still taken, as ib
    CHECK(o.notes().front().first == o.q / 4);
}

TEST_CASE("^ asks how to change the velocity: a value, a change or a percentage")
{
    Operating o;
    const auto prompt = [&]
    {
        // The prompt's row, just above the status line.
        const auto frame = o.ws.frame();
        const auto* bottom = &frame[frame.size() - 2];
        std::u32string text;
        for (const auto& run : bottom->text)
        {
            text += run.text;
        }
        return text;
    };
    o.type(U"gr^^"); // bar 1's chord, G1 at 80 and E1 at 96
    REQUIRE(o.ws.mode() == Mode::CommandLine);
    CHECK(prompt().starts_with(U"Velocity of 2 notes in Bass (80, +10, 80%): "));
    o.type(U"+10");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{90, 106, 50, 100, 70});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Change the velocity of 2 notes in Bass by +10");
    o.type(U"^^50%");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{40, 48, 50, 100, 70});
    o.type(U"ga^ib120"); // in the arrangement, the bar's events
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{120, 120, 50, 100, 70});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Set the velocity of bar 1 of Bass to 120");
    o.type(U"u");

    // Not a velocity: said so, and nothing changed; Escape leaves it; Tab doesn't complete.
    o.type(U"^^200");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message() ==
          "Not a velocity: 200; type a value (80), a change (+10, -10) or a percentage (80%)");
    o.type(U"^^8");
    o.press(NamedKey::Tab);
    CHECK(o.ws.commandLine().text() == U"8");
    o.press(NamedKey::Escape);
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.velocities() == std::vector{80, 96, 50, 100, 70});

    // The : prompt afterwards is the command line again.
    o.type(U":");
    CHECK(prompt().starts_with(U":"));
    o.press(NamedKey::Escape);
}

TEST_CASE(
    "humanising with ~ moves notes a little at random in time and velocity, up to the settings")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.ws.setHumanize(std::chrono::milliseconds{20}, 5);
    // Each note's start and velocity, by its pitch, since the chord's two may change places.
    const auto byPitch = [&]
    {
        std::map<int, std::pair<core::Tick, int>> found;
        const auto notes = o.notes();
        const auto velocities = o.velocities();
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            found[notes[i].second] = {notes[i].first, velocities[i]};
        }
        return found;
    };
    const auto before = byPitch();
    o.type(U"gr~2ib"); // the first two bars' notes, at 120 a minute: 20 ms is q/25
    const auto after = byPitch();
    REQUIRE(after.size() == before.size());
    for (const int pitch : {31, 28, 45})
    {
        CAPTURE(pitch);
        REQUIRE(after.contains(pitch));
        CHECK(std::abs(after.at(pitch).first - before.at(pitch).first) <= q / 25 + 1);
        CHECK(std::abs(after.at(pitch).second - before.at(pitch).second) <= 5);
    }
    CHECK(after.at(36) == before.at(36)); // the C2 in bar 5 is outside it
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Humanise 3 notes in Bass");

    // With nothing to change by, nothing changes.
    o.ws.setHumanize(std::chrono::milliseconds{0}, 0);
    o.type(U"~~");
    CHECK(o.ws.message() == "Nothing changed in 2 notes in Bass");
}

TEST_CASE("keys an operator can't take, and keys bound to nothing, say so and do nothing")
{
    Operating o;
    const auto before = o.notes();

    // Another operator's key isn't a range: only the operator's own takes the bar.
    o.type(U"gr");
    o.type(U"dt");
    CHECK(o.ws.message() == "t isn't a range or object after d");
    o.type(U">q");
    CHECK(o.ws.message() == "q isn't a range or object after >");
    // Keys that start ranges or objects but continue none: the whole of them is given up, not
    // the last taken alone (dat isn't dt, nor dd).
    o.type(U"dat");
    CHECK(o.ws.message() == "at isn't a range or object after d");
    o.type(U"dx");
    CHECK(o.ws.message() == "x isn't a range or object after d");
    CHECK(o.notes() == before);
    CHECK(o.ws.pending().empty());

    // In Normal mode too, with the leader as typed.
    o.type(U"Z");
    CHECK(o.ws.message() == "Z isn't a key here");
    o.type(U",Z");
    CHECK(o.ws.message() == ",Z isn't a key here");
    o.type(U"j"); // the next key works as ever, and the message goes
    CHECK(o.ws.message().empty());

    // The operator's own key still takes the bar.
    o.type(U"dd");
    CHECK(o.notes().size() == before.size() - 2);
}

TEST_CASE(". repeats the last change, with its range or object, as one undo step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto before = o.notes();
    o.type(U"gr>>"); // the piano roll: bar 1's notes up a semitone
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});
    o.type(U"4}"); // to bar 5, moving only: not a change
    o.type(U".");  // bar 5's C2 up too
    CHECK(o.notes()[3] == std::pair{16 * q + q / 16, 37});
    o.type(U"u"); // one step
    CHECK(o.notes()[3] == before[3]);
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32}); // the first change stays

    // A count replaces the change's.
    o.type(U"gg3.");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 35});
    o.type(U"uu");
    CHECK(o.notes() == before);

    // ^ repeats with the same answer, without asking.
    o.type(U"^^80");
    o.press(NamedKey::Enter);
    o.type(U"4}.");
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.velocities()[3] == 80);

    // Undo isn't a change to repeat.
    o.type(U"u.");
    CHECK(o.velocities()[3] == 80);
}

TEST_CASE(". after a Visual operator takes as much from the cursor")
{
    Operating o;
    o.type(U"gr");
    o.type(U"v}>"); // bar 1's first step to bar 2's: the G1 and E1 up
    const auto first = o.notes();
    o.type(U"gg4}"); // bar 5
    o.type(U".");
    CHECK(o.notes()[3].second == 37); // the C2 there, taken by as much from the cursor
    CHECK(o.notes().front() == first.front());
}

namespace
{

// Whether any text in the window has this in it, as the status line's.
bool shows(const Operating& o, std::u32string_view wanted)
{
    for (const auto& region : o.ws.frame())
    {
        for (const auto& text : region.text)
        {
            if (text.text.find(wanted) != std::u32string::npos)
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("Qa records the keys typed into macro a until Q, and @a plays them as one undo step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto before = o.notes();
    o.type(U"gr"); // the piano roll, at bar 1
    o.type(U"Qa");
    CHECK(shows(o, U"recording @a, Q ends it"));
    o.type(U">>}"); // bar 1's notes up a semitone, and on to bar 2
    o.type(U"Q");
    CHECK(o.ws.message() == "Recorded @a, 3 keys");
    CHECK_FALSE(shows(o, U"recording"));
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});

    o.type(U"@a"); // bar 2's A2 up, and on to bar 3
    CHECK(o.notes()[2] == std::pair{4 * q, 46});
    o.type(U"u");
    CHECK(o.notes()[2] == before[2]);
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});

    // @@ plays the last again; a count plays it that many times, as one step.
    o.type(U"gg}@@");
    CHECK(o.notes()[2] == std::pair{4 * q, 46});
    o.type(U"u");
    o.type(U"gg3@a"); // bars 1 to 3
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 33});
    CHECK(o.notes()[2] == std::pair{4 * q, 46});
    o.type(U"u");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});
    CHECK(o.notes()[2] == before[2]);
}

TEST_CASE("QA adds to a macro, :macros lists them, and . repeats one played")
{
    Operating o;
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U"gr");
    o.type(U"Qa>>Q");
    o.type(U"QA}Q");
    CHECK(o.ws.message() == "Recorded @a, 3 keys");
    o.type(U":macros");
    o.press(NamedKey::Enter);
    using Lines = std::vector<std::pair<std::string, std::string>>;
    CHECK(o.ws.listing() == Lines{{"@a", ">>}"}});
    o.press(NamedKey::Escape); // closes it

    o.type(U"@a"); // bar 2's A2, from where QA left the cursor
    CHECK(o.notes()[2].second == 46);
    o.type(U"gg."); // @a again, at bar 1
    CHECK(o.notes().front().second == 33);
    o.type(U"u");
    CHECK(o.notes().front().second == 32);
    CHECK(o.notes()[2].second == 46);
}

TEST_CASE("A command failing stops a macro, and macros playing macros stop 20 deep")
{
    Operating o;
    o.type(U"gr"); // at the song's start
    // h can't move from the start, which stops the macro after its first >>.
    o.type(U"Qa>>hQ");
    CHECK(o.notes().front().second == 32);
    o.type(U"5@a");
    CHECK(o.notes().front().second == 33);

    // A macro playing itself: each >> and @b again, until 20 deep.
    o.type(U"Qb>>@bQ"); // recording, @b is empty, which does nothing
    CHECK(o.notes().front().second == 34);
    o.type(U"@b");
    CHECK(o.ws.message() == "Macros playing macros stopped, 20 deep");
    CHECK(o.notes().front().second == 54);
    o.type(U"u"); // all one step
    CHECK(o.notes().front().second == 34);
}

TEST_CASE("Q and @ take a letter, Escape giving it up")
{
    Operating o;
    o.type(U"Q1");
    CHECK(o.ws.message() == "Macros are named by letters, Qa to Qz, a capital adding to one");
    CHECK_FALSE(shows(o, U"recording"));
    o.type(U"Q");
    o.press(NamedKey::Escape);
    CHECK_FALSE(shows(o, U"recording"));
    o.type(U"@z");
    CHECK(o.ws.message() == "@z is empty");
    o.type(U"@@");
    CHECK(o.ws.message() == "No macro played yet to play again");
}

TEST_CASE("ma marks the cursor's place, as an undo step, and 'a goes back to it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto bass = o.document.song().tracks()[0]->id();
    o.type(U"gr4}kk"); // the piano roll, bar 5, two semitones up
    const auto pitch = o.ws.pianoRoll().cursor().pitch;
    o.type(U"ma");
    const auto* mark = o.document.song().mark('a');
    REQUIRE(mark != nullptr);
    CHECK(mark->tick == 16 * q);
    CHECK(mark->track == bass);
    CHECK(pitch.has_value());
    CHECK(mark->pitch == static_cast<int>(pitch.value_or(0)));

    o.type(U"ggjjj'a");
    CHECK(o.ws.pianoRoll().cursor() == SongCursor{bass, 16 * q, pitch});
    // '' goes back to before the jump, and again comes back.
    o.type(U"''");
    CHECK(o.ws.pianoRoll().cursor().tick == 0);
    o.type(U"`'");
    CHECK(o.ws.pianoRoll().cursor().tick == 16 * q);

    // An undo step; set again, it moves.
    o.type(U"u");
    CHECK(o.document.song().mark('a') == nullptr);
    o.type(U"}ma");
    CHECK(o.document.song().mark('a')->tick == 20 * q);
    o.type(U"mb");
    CHECK(o.document.song().marks().size() == 2);

    // Letters only; m Space is still the play-from marker, and sets no mark.
    o.type(U"mA");
    CHECK(o.ws.message() == "Marks are named by letters, a to z");
    o.type(U"m ");
    CHECK(o.document.song().marks().size() == 2);
    o.type(U"'z");
    CHECK(o.ws.message() == "Mark z isn't set");
}

TEST_CASE("a mark is a range after an operator, to its time")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr4}ma"); // bar 5
    o.type(U"gg>'a");  // bars 1 to 4's notes up
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 46}, {16 * q + q / 16, 36}, {28 * q, 33}});
    CHECK(o.ws.pianoRoll().cursor().tick == 0);
    o.type(U"u");
    o.type(U"d'z"); // no mark z: the operator given up
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.notes().front().second == 31);
}

TEST_CASE("marks set outside the piano roll have no pitch, and going to one goes to its track")
{
    Operating o;
    const auto lead = o.document.song().tracks()[1]->id();
    o.type(U"j}mc"); // the arrangement: the lead's bar 2
    const auto* mark = o.document.song().mark('c');
    REQUIRE(mark != nullptr);
    CHECK(mark->track == lead);
    CHECK(mark->tick == 4 * o.q);
    CHECK(mark->pitch == core::Mark::kNoPitch);

    o.type(U"kgr"); // the bass in the piano roll
    o.type(U"'c");
    CHECK(o.ws.pianoRoll().cursor().track == lead);
    CHECK(o.ws.pianoRoll().cursor().tick == 4 * o.q);
}

TEST_CASE(":marks says where the marks are, :mark-delete deletes them, and the views show them")
{
    Operating o;
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U":marks");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message() == "No marks set");
    o.type(U"4}ma");   // the arrangement: the bass's bar 5
    o.type(U"gr}kmb"); // the piano roll: bar 6, at a pitch
    const auto pitch = core::pitchName(*o.ws.pianoRoll().cursor().pitch);
    o.type(U":marks");
    o.press(NamedKey::Enter);
    using Lines = std::vector<std::pair<std::string, std::string>>;
    CHECK(o.ws.listing() == Lines{{"a", "5:1:000, Bass"}, {"b", "6:1:000, Bass, " + pitch}});
    o.press(NamedKey::Escape);

    // The piano roll shows b among the notes, a in its ruler.
    CHECK(shows(o, U"b"));
    CHECK(o.registry.runLine("mark-delete a-b") == CommandResult{true, "Deleted marks a, b"});
    CHECK(o.document.song().marks().empty());
    o.type(U"u");
    CHECK(o.document.song().marks().size() == 2);
    CHECK(o.registry.runLine("mark-delete 9") ==
          CommandResult::error("Marks are named by letters: a, abc, or a-d"));
    CHECK(o.registry.runLine("mark-delete z").message == "No marks there to delete");
}

namespace
{

// The bass with a Bb1 in bar 3 and a B1 in bar 4 too.
void addFlats(Operating& o)
{
    const auto bass = o.document.song().tracks()[0]->id();
    const auto q = o.q;
    o.document.edit("Notes", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(bass,
                                          [&](core::EventList& events)
                                          {
                                              events.insert({8 * q, core::Note{34, 90, q}});
                                              events.insert({12 * q, core::Note{35, 90, q}});
                                          });
                    });
}

} // namespace

TEST_CASE("f and a note name go to the next note of that pitch class, and ; and Alt-; repeat it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto bass = o.document.song().tracks()[0]->id();
    const auto back = [&] { o.ws.handle(Key::typed(U';', kAlt), 0); };
    o.type(U";"); // nothing to repeat yet
    CHECK(o.ws.message() == "No find yet: f and a note name finds one");
    o.type(U"gr"); // the piano roll, at the start
    const auto at = [&] { return o.ws.pianoRoll().cursor(); };
    o.type(U"fa");
    CHECK(at() == SongCursor{bass, 4 * q, 45}); // A2, any octave
    o.type(U";");
    CHECK(at() == SongCursor{bass, 28 * q, 33}); // A1
    o.type(U";");
    CHECK(o.ws.message() == "No A after here in Bass");
    CHECK(at().tick == 28 * q);
    back(); // the other way
    CHECK(at().tick == 4 * q);
    // f always takes a name, in either case, and other keys between don't end it.
    o.type(U"jfC");
    CHECK(at() == SongCursor{bass, 16 * q + q / 16, 36});
    o.type(U"Fa");
    CHECK(at() == SongCursor{bass, 4 * q, 45});
    // F's ; goes back, and Alt-; on.
    o.type(U";");
    CHECK(o.ws.message() == "No A before here in Bass");
    CHECK(at().tick == 4 * q);
    back();
    CHECK(at().tick == 28 * q);
    // A count goes as many on.
    o.type(U"gg2fa");
    CHECK(at().tick == 28 * q);
    o.type(U"gg2;");
    CHECK(at().tick == 28 * q);
    // Anything but a note name is refused.
    o.type(U"jfh");
    CHECK(o.ws.message() == "f takes a note name after it, such as fE, fF# or fBb");
}

TEST_CASE("a sharp or flat after a find's letter goes again from where it started")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    addFlats(o);
    o.type(U"gr4l");
    o.type(U"fa"); // A2, in bar 2
    REQUIRE(o.ws.pianoRoll().cursor().tick == 4 * o.q);
    o.type(U"fb");
    CHECK(o.ws.pianoRoll().cursor().tick == 12 * q); // B1
    o.type(U"b");
    CHECK(o.ws.pianoRoll().cursor().tick == 8 * q); // Bb1, from bar 2 again
    CHECK(o.ws.pianoRoll().cursor().pitch == 34);
    o.type(U";");
    CHECK(o.ws.message() == "No Bb after here in Bass");
    o.type(U"}Fa"); // from bar 4, A2 in bar 2
    CHECK(o.ws.pianoRoll().cursor().tick == 4 * q);
    o.type(U"#"); // A#, which is Bb, from bar 4 again
    CHECK(o.ws.pianoRoll().cursor() == SongCursor{o.document.song().tracks()[0]->id(), 8 * q, 34});
    // In a macro, a find that finds nothing fails only once its name is done.
    // From just after Bb1, with no B before it but a Bb.
    o.type(U"gg}}lQaFbbQ");
    CHECK(o.ws.pianoRoll().cursor().tick == 8 * q);
    o.type(U"gg}}l@a");
    CHECK(o.ws.pianoRoll().cursor().tick == 8 * q);
    CHECK(o.ws.pianoRoll().cursor().pitch == 34);
}

TEST_CASE("after an operator, a find takes the range through the note it finds")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    addFlats(o);
    o.type(U"gr"); // the piano roll, at the start
    o.type(U"dfa");
    // At once, to A2's end, though a sharp or flat may yet change it.
    const Notes afterA{{8 * q, 34}, {12 * q, 35}, {16 * q + q / 16, 36}, {28 * q, 33}};
    CHECK(o.notes() == afterA);
    CHECK(o.ws.pending() == "dfA");
    // A sharp, however long after, does it again to A# (Bb1), as one undo step; . repeats it.
    o.ws.wake(10 * kDefaultTimeout);
    o.type(U"#");
    const Notes afterBb{{12 * q, 35}, {16 * q + q / 16, 36}, {28 * q, 33}};
    CHECK(o.notes() == afterBb);
    CHECK(o.ws.pending().empty());
    o.type(U"u");
    CHECK(o.notes().size() == 7);
    o.type(U".");
    CHECK(o.notes() == afterBb);
    o.type(U"u");
    o.type(U"dfbb");
    CHECK(o.notes() == Notes{{12 * q, 35}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"u");
    // A yank done again takes what the new range does, not both.
    o.type(U"yfa#");
    CHECK(o.ws.registerEvents().size() == 4);
    // Escape undoes it, the cursor where it was (decided November 2026), leaving nothing for .
    // to repeat; another key leaves it as it is, and is a key of its own, not part of the change
    // . repeats.
    o.type(U"dfa");
    o.press(NamedKey::Escape);
    CHECK(o.notes().size() == 7);
    CHECK(o.ws.message() == "Nothing done");
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.ws.pianoRoll().cursor().tick == 0);
    o.type(U".");
    CHECK(o.notes() == afterBb); // the last change, dfa#, still
    o.type(U"u");
    o.type(U"dfal");
    CHECK(o.notes() == afterA);
    o.type(U"#");
    CHECK(o.notes() == afterA);
    o.type(U"u");
    // ; after an operator is the find again, as f's: from A2 through A1.
    o.type(U"fad;");
    CHECK(o.notes().size() == 2);
    CHECK(std::ranges::none_of(o.notes(), [&](const auto& note) { return note.first >= 4 * q; }));
    o.type(U"u");
    o.type(U"gg}}dFaj");
    CHECK(o.notes().size() == 6); // from A2 in bar 2 to the cursor in bar 3
    const auto pitch = o.ws.pianoRoll().cursor().pitch;
    o.type(U"}.");
    CHECK(o.ws.pianoRoll().cursor().pitch == pitch);
    // Nothing to find gives the operator up, and with the sharp too.
    o.type(U"dfd");
    CHECK(o.ws.message() == "No D after here in Bass");
    CHECK(o.ws.mode() == Mode::Normal);
    o.type(U"#");
    CHECK(o.ws.message() == "No D# after here in Bass");
    CHECK(o.ws.mode() == Mode::Normal);
}

TEST_CASE("in the arrangement a find goes to the next bar with the note, and in the event list "
          "to its row")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"fa");
    CHECK(o.ws.arrangement().cursor().tick == 4 * q); // bar 2
    o.type(U";");
    CHECK(o.ws.arrangement().cursor().tick == 28 * q); // bar 8
    o.type(U"jfa");
    CHECK(o.ws.message() == "No A after here in Lead");
    // In Visual mode, the selection follows, and ; repeats.
    o.type(U"kggvfa;");
    CHECK(o.ws.mode() == Mode::Visual);
    CHECK(o.ws.arrangement().cursor().tick == 28 * q);
    o.press(NamedKey::Escape);
    // After an operator, through the bar it finds.
    o.type(U"ggdfc");
    CHECK(o.notes() == Notes{{28 * q, 33}});
    o.type(U"u");

    o.ws.show(ViewKind::EventList);
    o.type(U"ggfa");
    CHECK(o.ws.eventList().cursor().tick == 4 * q);
    o.type(U"ggdfa");
    CHECK(o.notes() == Notes{{16 * q + q / 16, 36}, {28 * q, 33}});
}

TEST_CASE("the hint menu says a find waits for a note name, and after an operator for a sharp or "
          "flat")
{
    Operating o;
    o.ws.setHints(true, 0);
    using Hints = std::vector<Workspace::Hint>;
    o.type(U"grf");
    CHECK(o.ws.hints() == Hints{{"c d e f g a b", "a note name, any octave", false},
                                {"# b", "then sharp or flat", false}});
    o.type(U"a");
    CHECK_FALSE(o.ws.hints());
    o.press(NamedKey::Escape);
    // After an operator, f among the ranges, then the sharp or flat.
    o.type(U"d");
    REQUIRE(o.ws.hints());
    CHECK(std::ranges::count(*o.ws.hints(),
                             Workspace::Hint{"f", "through the next note named", false}) == 1);
    o.type(U"fa");
    CHECK(o.ws.hints() == Hints{{"#", "sharp", false}, {"b", "flat", false}});
    o.type(U"j"); // until the next key
    CHECK_FALSE(o.ws.hints());
}

TEST_CASE("a key bound to nothing says so at once")
{
    Operating o;
    CHECK(o.ws.handle(Key::typed(U'%'), 0)); // drawn, rather than waiting for the next key
    CHECK(o.ws.message() == "% isn't a key here");
}

TEST_CASE("an operator's find with a sharp plays back from a macro as one undo step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    addFlats(o);
    const Notes afterBb{{12 * q, 35}, {16 * q + q / 16, 36}, {28 * q, 33}};
    o.type(U"grQadfa#Q");
    CHECK(o.notes() == afterBb);
    o.type(U"u");
    REQUIRE(o.notes().size() == 7);
    o.type(U"gg@a");
    CHECK(o.notes() == afterBb);
    o.type(U"u");
    CHECK(o.notes().size() == 7);
    // Without the sharp, at the macro's end, as it is.
    o.type(U"QbdfaQugg@b");
    CHECK(o.notes().size() == 4);
}

namespace
{

// Typing a search at the / prompt, and Enter.
void search(Operating& o, std::u32string_view query)
{
    o.type(U"/");
    o.type(query);
    o.press(NamedKey::Enter);
}

} // namespace

TEST_CASE("searching with / goes to the next match on the track, and n and N to the next "
          "and the one before, round past the ends")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto bass = o.document.song().tracks()[0]->id();
    o.type(U"gr"); // the piano roll, at the start
    const auto at = [&] { return o.ws.pianoRoll().cursor(); };
    const auto status = [&] { return o.ws.pianoRoll().matchStatus(*o.ws.searchQuery()); };
    o.type(U"n");
    CHECK(o.ws.message() == "No search yet: / searches");
    search(o, U"a");
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(at() == SongCursor{bass, 4 * q, 45});
    CHECK(status() == "match 1/2");
    o.type(U"n");
    CHECK(at() == SongCursor{bass, 28 * q, 33});
    o.type(U"n");
    CHECK(at().tick == 4 * q);
    CHECK(o.ws.message() == "Past the last match, round to the first");
    o.type(U"N");
    CHECK(at().tick == 28 * q);
    CHECK(o.ws.message() == "Before the first match, round to the last");
    // A jump, which '' goes back from.
    o.type(U"''");
    CHECK(at().tick == 4 * q);
    // Every term matching; a pitch exact.
    search(o, U"A1");
    CHECK(at() == SongCursor{bass, 28 * q, 33});
    o.type(U"n");
    CHECK(o.ws.message() == "This is the only match for A1");
    search(o, U"vel<60 len>=1/4");
    CHECK(at().tick == 4 * q);
    search(o, U"d");
    CHECK(o.ws.message() == "No match for d on this track");
    // Matches follow edits.
    search(o, U"a");
    o.type(U"x");
    CHECK(status() == "1 match");
    o.type(U"u");
    // What can't be read says why; / and Enter alone clear the matches; Escape leaves them.
    search(o, U"h2");
    CHECK(o.ws.message() ==
          "Can't search for \"h2\": try a pitch (C3), vel>100, len<1/16, cc64 or bend");
    o.type(U"/");
    o.press(NamedKey::Escape);
    CHECK(o.ws.searchQuery());
    o.type(U"/");
    o.press(NamedKey::Enter);
    CHECK_FALSE(o.ws.searchQuery());
    search(o, U"a");
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U":noh");
    o.press(NamedKey::Enter);
    CHECK_FALSE(o.ws.searchQuery());
}

TEST_CASE("* and # search for the pitch under the cursor, and n keeps to their way")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    addFlats(o);
    o.document.edit("Notes", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(song.tracks()[0]->id(), [&](core::EventList& events)
                                          { events.insert({20 * q, core::Note{45, 90, q}}); });
                    });
    o.type(U"grfa"); // A2 in bar 2
    o.press(NamedKey::Escape);
    o.type(U"*");
    CHECK(o.ws.pianoRoll().cursor().tick == 20 * q); // the other A2, not A1
    o.type(U"#");
    CHECK(o.ws.pianoRoll().cursor().tick == 4 * q);
    o.type(U"n"); // backwards, as # went
    CHECK(o.ws.pianoRoll().cursor().tick == 20 * q);
    CHECK(o.ws.message() == "Before the first match, round to the last");
}

TEST_CASE("after an operator, n takes the range up to the next match")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    search(o, U"c2");
    o.type(U"ggdn");
    CHECK(o.notes() == Notes{{16 * q + q / 16, 36}, {28 * q, 33}}); // up to C2, not it
}

TEST_CASE("searching in the event list goes to rows, and in the arrangement to bars")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    search(o, U"a");
    CHECK(o.ws.arrangement().cursor().tick == 4 * q);
    CHECK(o.ws.arrangement().matchStatus(*o.ws.searchQuery()) == "bar 1/2 with matches");
    o.type(U"n");
    CHECK(o.ws.arrangement().cursor().tick == 28 * q);

    o.ws.show(ViewKind::EventList);
    o.type(U"gg");
    search(o, U"cc064>63");
    CHECK(o.ws.eventList().cursor().tick == 16 * q + q / 8);
    CHECK(o.ws.eventList().matchStatus(*o.ws.searchQuery()) == "match 1/1");
    o.type(U"gg*"); // the conductor's rows first
    CHECK(o.ws.message() == "No note or controller here to search for");
    search(o, U"g1");
    o.type(U"*");
    CHECK(o.ws.message() == "This is the only match for G1");
}

TEST_CASE("a match is outlined in the piano roll, and its velocity in the lane in the match's "
          "colour")
{
    Operating o;
    o.type(U"gr");
    search(o, U"vel<60"); // A2, in bar 2
    o.type(U"gg");        // off it, so it isn't the current note
    const auto frame = o.ws.frame();
    std::size_t shapes = 0;
    for (const auto& region : frame)
    {
        shapes += static_cast<std::size_t>(std::ranges::count_if(
            region.under, [](const Shape& shape) { return shape.role == Role::Match; }));
    }
    CHECK(shapes == 5); // the outline's four sides, and the velocity's bar
}

TEST_CASE("the leader's s selects an area, or with a capital only the matches in it, which "
          "operators take")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    o.type(U",ST");
    CHECK(o.ws.message() == "No search to select the matches of: / searches first");
    CHECK(o.ws.mode() == Mode::Normal);
    search(o, U"a"); // A2 and A1
    o.type(U",ST>");
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.notes() == Notes{{0, 31}, {0, 28}, {4 * q, 46}, {16 * q + q / 16, 36}, {28 * q, 34}});
    CHECK(o.ws.message().empty());
    o.type(U"u");
    o.type(U",STd");
    CHECK(o.notes() == Notes{{0, 31}, {0, 28}, {16 * q + q / 16, 36}});
    CHECK(o.ws.message() == "Deleted 2 matches");
    o.type(U"u");
    // Narrowing a selection to its matches; yanking them.
    o.type(U"gg");
    o.type(U"vit,Smy");
    CHECK(o.ws.registerEvents().size() == 2);
    o.type(U"}");
    o.type(U",SBd"); // bar 2's A2 only
    CHECK(o.notes().size() == 4);
    o.type(U"u");
    // Lower case, the area itself.
    o.type(U"gg,Sbd");
    CHECK(o.notes().size() == 3); // bar 1's G1 and E1
    o.type(U"u");
    o.type(U"gg,SV");
    CHECK(o.ws.mode() == Mode::Visual);
    // Time can't be taken from matches.
    o.type(U"D");
    CHECK(o.ws.message() == "D takes out the selection's time, not its matches");
    o.type(U",ST");
    o.type(U"p");
    CHECK(o.ws.message() == "p puts over the selection's time, not its matches");
    // , s c clears the search.
    o.type(U",Sc");
    CHECK_FALSE(o.ws.searchQuery());
}

TEST_CASE("Up and Down at the search prompt recall earlier searches, apart from the : line's")
{
    Operating o;
    o.type(U"gr");
    search(o, U"a");
    search(o, U"c2");
    o.type(U":");
    o.press(NamedKey::Escape);
    o.type(U"/");
    o.press(NamedKey::Up);
    o.press(NamedKey::Up);
    o.press(NamedKey::Enter);
    CHECK(o.ws.pianoRoll().matchStatus(*o.ws.searchQuery()) == "match 2/2"); // a, from C2
    o.type(U"/");
    o.press(NamedKey::Up);
    o.press(NamedKey::Down);
    o.press(NamedKey::Enter); // back to the empty line, which clears
    CHECK_FALSE(o.ws.searchQuery());
}

TEST_CASE("gn after an operator changes the match at the cursor or the next, going on to the "
          "next, which . changes and n skips, the status line saying so")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    o.type(U"dgn");
    CHECK(o.ws.message() == "No search yet: / searches");
    CHECK(o.ws.mode() == Mode::Normal);
    search(o, U"a"); // A2 (velocity 50), then A1 (70)
    o.type(U"^gn+10");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{80, 96, 60, 100, 70});
    CHECK(o.ws.message() == "Changed match 1/2 · on to the next: . changes it, n skips it");
    CHECK(o.ws.pianoRoll().cursor().tick == 28 * q);
    o.type(U".");
    CHECK(o.velocities() == std::vector<int>{80, 96, 60, 100, 80});
    CHECK(o.ws.message() == "Changed match 2/2 · on to the next: . changes it, n skips it");
    CHECK(o.ws.pianoRoll().cursor().tick == 4 * q); // round to the first again
    o.type(U"n");
    CHECK(o.ws.pianoRoll().cursor().tick == 28 * q);
    CHECK(o.ws.message() == "Skipped to match 2/2 · . changes it, n skips again");
    // Any other key ends the run.
    o.type(U"jn");
    CHECK(o.ws.message().find("Skipped") == std::string::npos);
    // d and y take the match; one no longer matching after a change moves on too.
    o.type(U">gn");
    CHECK(o.notes()[2] == std::pair{core::Tick{4 * q}, 46});
    CHECK(o.ws.pianoRoll().cursor().tick == 28 * q);
    o.type(U"ygn");
    CHECK(o.ws.registerEvents().size() == 1);
    CHECK(o.ws.message() == "Yanked match 1/1");
    CHECK(o.ws.pianoRoll().cursor().tick == 28 * q);
    o.type(U"dgn");
    CHECK(o.notes().size() == 4);
    CHECK(o.ws.message() == "Deleted match 1/1 · no other match");
}

TEST_CASE("gn without an operator selects the match, only it")
{
    Operating o;
    o.type(U"gr");
    search(o, U"a");
    o.type(U"gggn");
    CHECK(o.ws.mode() == Mode::Visual);
    CHECK(o.ws.message() == "The match selected · an operator changes it");
    o.type(U"d");
    CHECK(o.notes().size() == 4);
}

TEST_CASE("The transform key and a range open the transform menu at once, and its key transforms "
          "the range")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.ws.setHints(true, 400);
    o.type(U"gr");
    o.type(U"!ib");
    CHECK(o.ws.pending() == ",x");
    const auto hints = o.ws.hints();
    REQUIRE(hints);
    CHECK(std::ranges::find(*hints,
                            Workspace::Hint{.keys = "t", .title = "Transpose by semitones"}) !=
          hints->end());
    o.type(U"t+12");
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 43}, {0, 40}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    CHECK(o.ws.pending().empty());
    // . does it again, as it was answered.
    o.type(U".");
    CHECK(o.notes() == Notes{{0, 55}, {0, 52}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    // Escape gives the menu up, and its range with it: t is then shifting's operator.
    o.type(U"!ib");
    o.press(NamedKey::Escape);
    CHECK(o.ws.pending().empty());
    CHECK_FALSE(o.ws.hints());
    o.type(U"u");
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 40});
}

TEST_CASE("The transform menu's keys before a range are an operator, and on a selection act at "
          "once")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    o.type(U",xtib");
    CHECK(o.ws.mode() == Mode::CommandLine);
    o.type(U"-3");
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 28}, {0, 25}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U",xtibx");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message() == "Not a number of semitones: x; type +12, -3 or 7");
    o.type(U"vib!t2");
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 30}, {0, 27}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    CHECK(o.ws.mode() == Mode::Normal);
}

TEST_CASE(":transpose takes the selection, or else the cursor's bar, asking when given nothing")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U"gr:transpose 12");
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 43}, {0, 40}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"v]b]b:transpose -1"); // bars 1 and 2
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 42}, {0, 39}, {4 * q, 44}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"gg:transpose");
    o.press(NamedKey::Enter);
    CHECK(o.ws.mode() == Mode::CommandLine);
    o.type(U"+1");
    o.press(NamedKey::Enter);
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 40});
}

TEST_CASE("A count before an operator with no how far of its own counts bars, as 2dd does")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr2!!t1");
    o.press(NamedKey::Enter);
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 46}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"2^^90");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{90, 90, 90, 100, 70});
    // Bars 4 and 5: the C2 a little after bar 5's start.
    o.type(U"3]b2qqn"); // n: the pedal stays
    CHECK(o.notes()[3] == std::pair{core::Tick{16 * q}, 36});
}

TEST_CASE("Lifting and lowering velocities take their default at Enter, and limits and ramps ask")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U"gr!!l");
    CHECK(o.ws.mode() == Mode::CommandLine);
    o.press(NamedKey::Enter); // 25%
    CHECK(o.velocities() == std::vector<int>{92, 104, 50, 100, 70});
    o.type(U"u,xLib50");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{41, 49, 50, 100, 70});
    CHECK(o.ws.message().empty());
    o.type(U"u");
    // The cursor's bar: only the chord, at the ramp's start.
    o.type(U":vel-ramp 60-100");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{60, 60, 50, 100, 70});
    // Four bars: 60 at the chord, 100 at the A2.
    o.type(U"u4!!r60-100");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{60, 60, 100, 100, 70});
    o.type(U"u:vel-limit 85-90");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{85, 90, 50, 100, 70});
    o.type(U"u!!M90-85");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message() ==
          "Not a range of velocities: 90-85; type the lowest and the highest, as 20-110");
}

TEST_CASE("Relative velocity changes ease off near 127, or clip with velclip hard")
{
    Operating o;
    o.type(U"gr^^+40");
    o.press(NamedKey::Enter);
    const auto soft = o.velocities();
    CHECK(soft[0] < soft[1]);
    CHECK(soft[1] < 127);
    o.type(U"u");
    o.ws.setVelocityLimits({.soft = false, .floor = 1});
    o.type(U"^^+40");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector<int>{120, 127, 50, 100, 70});
}

TEST_CASE("Compressing velocities asks for its settings in turn, and the chord tools take a "
          "chord's notes")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto enter = [&](int times)
    {
        for (int i = 0; i < times; ++i)
        {
            o.press(NamedKey::Enter);
        }
    };
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    // Bar 1's chord: G1 at 80, E1 at 96; the A2 in bar 2 at 50 is outside a bar's window.
    o.type(U"gr2!!c");
    enter(4); // 50%, over a bar, towards the local average, all of them
    CHECK(o.velocities() == std::vector<int>{84, 92, 50, 100, 70});
    // Those the : line gives aren't asked for.
    o.type(U"u:vel-compress 100% threshold=>90");
    enter(1);
    CHECK(o.ws.mode() == Mode::CommandLine);
    enter(2); // the window and the centre
    CHECK(o.velocities() == std::vector<int>{80, 88, 50, 100, 70});
    o.type(U"u:vel-compress 50% speed=2");
    enter(4);
    CHECK(o.ws.message() == "Compress takes window=, centre= and threshold=, not speed=");
    o.type(U"2!!c");
    enter(1);
    o.type(U"2bars"); // the A2 now in the chord's window
    enter(3);
    CHECK(o.velocities()[2] > 50);
    o.type(U"u");
    // The top note up and the others down; then a slope from the bottom.
    o.type(U"!!a");
    enter(1);
    CHECK(o.velocities() == std::vector<int>{90, 86, 50, 100, 70});
    o.type(U"u,xbib");
    enter(1);
    CHECK(o.velocities() == std::vector<int>{95, 70, 50, 100, 70});
}

TEST_CASE("Each setting a transform asks for shows how it's written, and Tab its choices")
{
    Operating o;
    const auto hint = [&]
    {
        std::u32string dimmed;
        const auto frame = o.ws.frame();
        for (const auto& run : frame[frame.size() - 2].text)
        {
            if (run.style.foreground == Role::Dim)
            {
                dimmed = run.text;
            }
        }
        return dimmed;
    };
    const auto line = [&] { return o.ws.commandLine().text(); };
    o.type(U"gr!!c");
    CHECK(hint().empty()); // how much: its default says how it's written
    o.press(NamedKey::Enter);
    CHECK(hint() == U"1bar|2bars|1/2");
    o.type(U"2");
    o.press(NamedKey::Tab);
    CHECK(line() == U"2bars ");
    o.press(NamedKey::Enter);
    CHECK(hint() == U"local|average|N");
    o.type(U"a");
    o.press(NamedKey::Tab);
    CHECK(line() == U"average ");
    o.press(NamedKey::Enter);
    CHECK(hint() == U"all|>N|<N");
    o.press(NamedKey::Enter);
    CHECK(o.ws.mode() == Mode::Normal);
    // Others given as NAME=VALUE with an answer aren't asked for, a comma between them taken.
    o.type(U"u!!c50%, window=1bar");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.velocities() == std::vector<int>{84, 92, 50, 100, 70});
}

TEST_CASE("Transforms in a key ask for it, the last offered again, with Up for those before")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto line = [&] { return o.ws.commandLine().text(); };
    // Bar 1's chord: G1 and E1.
    o.type(U"gr,xTib");
    CHECK(o.ws.mode() == Mode::CommandLine);
    o.type(U"D lyd");
    o.press(NamedKey::Tab);
    CHECK(line() == U"D lydian ");
    o.press(NamedKey::Backspace);
    for (int i = 0; i < 6; ++i)
    {
        o.press(NamedKey::Backspace);
    }
    o.type(U"dorian");
    o.press(NamedKey::Enter);
    CHECK(o.ws.mode() == Mode::CommandLine); // then how far
    o.type(U"+1");
    o.press(NamedKey::Enter);
    CHECK(o.notes()[0] == std::pair{core::Tick{0}, 33}); // G1 to A1
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 29}); // E1 to F1
    // The key offered again, Enter taking it; Up recalls it as typed.
    o.type(U"!!T");
    o.press(NamedKey::Up);
    CHECK(line() == U"D dorian");
    o.press(NamedKey::Enter);
    o.type(U"-1");
    o.press(NamedKey::Enter);
    CHECK(o.notes()[0] == std::pair{core::Tick{0}, 31});
    // At the : prompt, with key= and the amount: nothing asked.
    o.ws.setLineHandler([&](const std::string& text) { o.registry.runLine(text); });
    o.type(U":transpose-key +7 key=C major");
    o.press(NamedKey::Enter);
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.notes()[0] == std::pair{core::Tick{0}, 43}); // G2
}

TEST_CASE("Inverting goes about the first note, at Enter, and retrograde reverses the notes")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr!!i");
    o.press(NamedKey::Enter);                            // about E1, the lowest of the first chord
    CHECK(o.notes()[0] == std::pair{core::Tick{0}, 25}); // G1 to C#1
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 28});
    o.type(U"u2!!R"); // bars 1 and 2: the chord's eighth and the A2's quarter
    CHECK(o.notes()[0] == std::pair{core::Tick{0}, 45});
    CHECK(o.notes()[1].first == 4 * q + q / 2);
}

TEST_CASE("Searching outside a key finds the notes not in its scale")
{
    Operating o;
    o.type(U"gr");
    search(o, U"outside D major"); // the C2 alone
    CHECK(o.ws.pianoRoll().cursor().tick == 16 * o.q + o.q / 16);
    CHECK(o.ws.pianoRoll().matchStatus(*o.ws.searchQuery()) == "match 1/1");
}

TEST_CASE("The search prompt says what a search can look for, and Tab completes its keywords")
{
    Operating o;
    const auto line = [&] { return o.ws.commandLine().text(); };
    const auto hint = [&]
    {
        std::u32string dimmed;
        const auto frame = o.ws.frame();
        for (const auto& run : frame[frame.size() - 2].text)
        {
            if (run.style.foreground == Role::Dim)
            {
                dimmed = run.text;
            }
        }
        return dimmed;
    };
    o.type(U"gr/");
    CHECK(hint() == U"C4 F# >A3 A3-A5 vel<60 len<1/16 cc1=127 bend inside KEY outside KEY");
    o.type(U"ve");
    CHECK(hint().empty());
    o.press(NamedKey::Tab);
    CHECK(line() == U"vel"); // going on, with a comparison
    o.type(U">90 out");
    o.press(NamedKey::Tab);
    CHECK(line() == U"vel>90 outside ");
    o.type(U"D lyd");
    o.press(NamedKey::Tab);
    CHECK(line() == U"vel>90 outside D lydian ");
}

TEST_CASE("The key question offers the key the notes fit best, and Tab the best few")
{
    const auto q = core::kPpqn.quarter();
    core::Song song{core::kPpqn};
    const auto piano = song.addTrack("Piano");
    song.updateEvents(
        piano,
        [&](core::EventList& events)
        {
            // In D major: D, F#, A, D, C#, E, D.
            core::Tick at = 0;
            for (const auto& [pitch, beats] : std::vector<std::pair<int, int>>{
                     {62, 2}, {66, 1}, {69, 1}, {74, 2}, {73, 1}, {64, 1}, {62, 4}})
            {
                events.insert({at, core::Note{static_cast<std::uint8_t>(pitch), 80, beats * q}});
                at += beats * q;
            }
        });
    song.fitToContent();
    core::Document document{std::move(song)};
    CommandRegistry registry;
    Workspace ws{registry, document};
    ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
    ws.show(ViewKind::PianoRoll);
    for (const char32_t key : std::u32string_view{U"3!!T"})
    {
        ws.handle(Key::typed(key), 0);
    }
    ws.handle(Key::of(NamedKey::Tab), 0);
    REQUIRE(ws.completions());
    CHECK(ws.completions()->matches.front() == "D major");
    ws.handle(Key::of(NamedKey::Enter), 0); // none chosen from the list: D major, offered
    for (const char32_t key : std::u32string_view{U"+1"})
    {
        ws.handle(Key::typed(key), 0);
    }
    ws.handle(Key::of(NamedKey::Enter), 0);
    // F#4 up a degree of D major: G4, where C major would make it G#4.
    const auto& events = document.song().events(document.song().tracks()[0]->id());
    CHECK(std::get<core::Note>(events[1].data).pitch == 67);
}

TEST_CASE("The menu's quantise asks for the grid, strength, swing and what to move, Enter "
          "throughout doing what q does")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto enter = [&](int times)
    {
        for (int i = 0; i < times; ++i)
        {
            o.press(NamedKey::Enter);
        }
    };
    const auto label = [&]
    {
        const auto frame = o.ws.frame();
        return frame[frame.size() - 2].text.front().text;
    };
    // The C2 a sixty-fourth after bar 5's start, the pedal just after it.
    o.type(U"gr4]b!!q");
    CHECK(label() == U"Quantise C2 in Bass, to a grid of (1/16): ");
    enter(4);
    CHECK(o.ws.message() == "Move the pedalling with its notes? (y/n)");
    o.type(U"n");
    CHECK(o.notes()[3] == std::pair{core::Tick{16 * q}, 36});
    o.type(U"u!!q");
    enter(1);
    o.type(U"50%");
    enter(3);
    o.type(U"n");
    CHECK(o.notes()[3] == std::pair{core::Tick{16 * q + q / 32}, 36});
    // Its end onto the grid, its start kept: no question about the pedalling.
    o.type(U"u!!q");
    enter(3);
    o.type(U"ends");
    enter(1);
    const auto& events = o.document.song().events(o.document.song().tracks()[0]->id());
    const auto c2 = std::ranges::find_if(events.all(), [&](const core::Event& e)
                                         { return e.tick == 16 * q + q / 16; });
    REQUIRE(c2 != events.all().end());
    CHECK(std::get<core::Note>(c2->data).duration == 3 * q / 16);
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    o.type(U"u:quantize 1/8 swing=90");
    enter(1);
    // Given the grid and the swing, asking for the strength and what to move.
    enter(2);
    CHECK(o.ws.message() == "Not a swing: 90; type 25% to 75%, 50% straight");
}

TEST_CASE("Stretch asks for a ratio, and note lengths for legato, a length or a percentage")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto lengths = [&]
    {
        std::vector<core::Tick> found;
        const auto& song = o.document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                found.push_back(note->duration);
            }
        }
        return found;
    };
    // Bars 1 and 2: the chord, eighths at the start, and the A2, a quarter at bar 2's.
    o.type(U"gr2!!s2");
    o.press(NamedKey::Enter);
    CHECK(o.notes()[2] == std::pair{core::Tick{8 * q}, 45});
    CHECK(lengths()[0] == q);
    o.type(U"u2!!nlegato");
    o.press(NamedKey::Enter);
    CHECK(lengths() == std::vector<core::Tick>{4 * q, 4 * q, q, q / 4, 4 * q});
    o.type(U"u!!n1/8");
    o.press(NamedKey::Enter);
    CHECK(lengths() == std::vector<core::Tick>{q / 2, q / 2, q, q / 4, 4 * q});
    o.type(U"u!!n200%");
    o.press(NamedKey::Enter);
    CHECK(lengths() == std::vector<core::Tick>{q, q, q, q / 4, 4 * q});
    o.type(U"u!!s0");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message().starts_with("Not a ratio: 0"));
}

TEST_CASE("Filter deletes the search's matches, split moves notes to a track, new or not, and "
          "remap changes a note")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto& song = o.document.song();
    o.type(U"gr");
    search(o, U"a"); // the A2 in bar 2 and the A1 in bar 8
    o.type(U"gg4!!f");
    o.press(NamedKey::Enter); // the search's, at Enter
    CHECK(o.notes().size() == 4);
    CHECK(o.notes()[2].second == 36);
    o.type(U"u");
    // Bar 1's chord: G1 up, to a new track after the bass, named after it.
    o.type(U"!!yG1");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter); // above
    o.press(NamedKey::Enter); // Bass high
    REQUIRE(song.tracks().size() == 3);
    CHECK(song.tracks()[1]->name == "Bass high");
    CHECK(song.events(song.tracks()[1]->id()).size() == 1);
    CHECK(o.notes().front().second == 28);
    o.type(U"u");
    CHECK(song.tracks().size() == 2);
    // To a track there is, both notes, naming it as an option.
    o.type(U"!!yE1 to=lead");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    CHECK(song.events(song.tracks()[1]->id()).size() == 2);
    o.type(U"u");
    // A note made another.
    o.type(U"!!mG1");
    o.press(NamedKey::Enter);
    o.type(U"A1");
    o.press(NamedKey::Enter);
    CHECK(o.notes().front().second == 33);
    // The pedals aren't thinned.
    o.type(U"!!dcc64");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    CHECK(o.ws.message().starts_with("cc64 is a switch"));
}

TEST_CASE("The pedalling tools act on the range's time in the piano roll, where it takes notes")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    // Bars 1 and 2: down just after the chord, lifted and pressed again around the A2, lifted
    // as it ends.
    o.type(U"gr2!!Pr");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    const auto pedals = o.pedals();
    REQUIRE(pedals.size() == 5);
    CHECK(pedals[0] > 0);
    CHECK(pedals[1] < 4 * q);
    CHECK(pedals[2] > 4 * q);
    CHECK(pedals[3] == 5 * q);
    CHECK(pedals[4] == 16 * q + q / 8); // bar 5's, outside the range
    o.type(U"u");
    // Bar 5's pedal cleared; pressed again at the bar's end, as it was down there.
    o.type(U"4]b!!Px");
    o.press(NamedKey::Enter);
    CHECK(o.pedals() == std::vector<core::Tick>{20 * q});
}

TEST_CASE("Every transform takes the defaults it offers, Enter throughout")
{
    for (const auto& spec : Workspace::transformSpecs())
    {
        Operating o;
        o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
        o.type(U"gr:");
        o.type(decodeUtf8(spec.name));
        o.press(NamedKey::Enter);
        for (int i = 0; i < 8 && o.ws.mode() == Mode::CommandLine; ++i)
        {
            o.press(NamedKey::Enter);
        }
        INFO(spec.name << ": " << o.ws.message());
        CHECK(o.ws.mode() == Mode::Normal);
        CHECK(o.ws.messageKind() != MessageKind::Warning);
    }
}

TEST_CASE("No two commands share a name or an alias")
{
    Operating o;
    std::map<std::string, std::string> taken;
    for (const auto& info : o.registry.all())
    {
        std::vector<std::string> names{std::string{info.name}};
        for (std::size_t at = 0; at < info.aliases.size();)
        {
            const auto end = std::min(info.aliases.find(' ', at), info.aliases.size());
            if (end > at)
            {
                names.emplace_back(info.aliases.substr(at, end - at));
            }
            at = end + 1;
        }
        for (const auto& name : names)
        {
            const auto [it, added] = taken.emplace(name, std::string{info.name});
            INFO(name << " is " << it->second << "'s and " << info.name << "'s");
            CHECK(added);
        }
    }
}

TEST_CASE("Transforms at their edges: across tracks, names with spaces, before the start, the end")
{
    Operating o;
    const auto q = o.q;
    auto& song = o.document;
    o.ws.setLineHandler([&](const std::string& line) { o.registry.runLine(line); });
    const auto lead = song.song().tracks()[1]->id();
    song.edit("Add a note", {},
              [&](core::Song& s)
              {
                  s.updateEvents(lead, [&](core::EventList& e)
                                 { e.insert({4 * q, core::Note{60, 90, q}}); });
              });

    // Two tracks stretched together, about the first event of both: the lead's note at bar 2
    // goes to bar 3, keeping time with the bass.
    o.type(U"vl:stretch 2"); // bars 1 and 2, on every track
    o.press(NamedKey::Enter);
    CHECK(song.song().events(lead)[0].tick == 8 * q);
    o.type(U"u");

    // A track's name with a space in it, at the : prompt.
    o.type(U"gr:split C1 side=above to=Right Hand");
    o.press(NamedKey::Enter);
    REQUIRE(song.song().tracks().size() == 3);
    CHECK(song.song().tracks()[1]->name == "Right Hand");
    o.type(U"u");
    // And the same typed in answer to split's first question.
    o.type(U"gr:split");
    o.press(NamedKey::Enter);
    o.type(U"C1 side=above to=Right Hand");
    o.press(NamedKey::Enter);
    REQUIRE(song.song().tracks().size() == 3);
    CHECK(song.song().tracks()[1]->name == "Right Hand");
    o.type(U"u");

    // A pedal offset that would go before the song's start is refused, not put at it.
    o.type(U"gg4]b:pedal-offset -9000ms"); // bar 5
    o.press(NamedKey::Enter);
    CHECK(o.ws.message().ends_with("would go before the song's start"));
    CHECK(o.pedals() == std::vector<core::Tick>{16 * q + q / 8});

    // Quantising starts and ends never puts a start on the song's end.
    song.edit("Add a note", {},
              [&](core::Song& s)
              {
                  s.updateEvents(s.tracks()[0]->id(), [&](core::EventList& e)
                                 { e.insert({32 * q - q / 64, core::Note{80, 90, q / 128}}); });
              });
    const auto end = song.song().end();
    o.type(U"G:quantize 1/16 move=both");
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    o.press(NamedKey::Enter);
    CHECK(song.song().end() == end);
}

TEST_CASE("In the piano roll, d0 and d$ take from the bar's start, and to its end")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto bass = o.document.song().tracks()[0]->id();
    o.type(U"gr");
    // Bar 2 holds the A2, at its start: from a beat in, d0 takes it, d$ doesn't.
    o.ws.pianoRoll().setCursor({.track = bass, .tick = 5 * q, .pitch = 45});
    o.type(U"d$");
    CHECK(o.notes().size() == 5);
    o.ws.pianoRoll().setCursor({.track = bass, .tick = 5 * q, .pitch = 45});
    o.type(U"d0");
    CHECK(o.notes().size() == 4);
    o.type(U"u");
    o.ws.pianoRoll().setCursor({.track = bass, .tick = 4 * q, .pitch = 45});
    o.type(U"d$");
    CHECK(o.notes().size() == 4);
}

TEST_CASE("While an operator waits, the status line says what it will do, and what doubled takes")
{
    Operating o;
    const auto status = [&]
    {
        std::string text;
        const auto frame = o.ws.frame();
        for (const auto& run : frame.back().text)
        {
            text += encodeUtf8(run.text);
        }
        return text;
    };
    o.type(U"gr3>");
    INFO(status());
    CHECK(status().find("Transpose up 3 semitones · then a range or object; >> ") !=
          std::string::npos);
    o.press(NamedKey::Escape);
    o.type(U"2q");
    INFO(status());
    CHECK(status().find("Quantise · then a range or object; qq ") != std::string::npos);
    CHECK(status().find(", and 1 more") != std::string::npos);
}

TEST_CASE("A count after an operator isn't for gn, which takes one match, and says so")
{
    Operating o;
    o.type(U"gr");
    search(o, U"a");
    o.type(U"d2gn");
    CHECK(o.ws.message() == "gn takes the one match; . does the next");
    CHECK(o.notes().size() == 5);
}

TEST_CASE("A macro stops where a command is refused, whatever its message, and only there")
{
    // A command's outcome apart from its message (decided November 2026).
    Operating o;
    // u with nothing to undo is refused, though its message isn't a warning: j isn't reached.
    o.type(U"QaujQ");
    REQUIRE(o.ws.arrangement().page().track() == 1);
    o.type(U"k@a");
    CHECK(o.ws.message() == "Nothing to undo");
    CHECK(o.ws.refused());
    CHECK(o.ws.arrangement().page().track() == 0);

    // A question, shown as a warning, is answered by the macro's next key and it goes on.
    const auto q = o.q;
    o.ws.setQuantizePedal(Workspace::QuantizePedal::Ask);
    o.type(U"4lQbqqyjQ"); // bar 5, the C2 and its pedal, then down a track
    o.type(U"u");         // back on the first track, at bar 5
    REQUIRE(o.notes()[3] == std::pair{16 * q + q / 16, 36});
    o.type(U"@b");
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 16});
    CHECK(o.ws.arrangement().page().track() == 1);
    CHECK_FALSE(o.ws.refused());
}

TEST_CASE("A macro continues after a section split that warns about a short bar")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const bool colon = GENERATE(false, true);
    Operating o{ppqn};
    o.ws.setLineHandler([&](const std::string& line) { o.ws.report(o.registry.runLine(line)); });
    const auto track = o.document.song().tracks()[0]->id();
    o.document.edit("Track meter", {},
                    [&](core::Song& song)
                    {
                        song.updateMeter(track, [](core::MeterMap& meter)
                                         { meter = core::MeterMap{meter.ppqn(), {3, 4}}; });
                    });
    o.ws.refresh();
    o.type(U"gr");
    o.ws.pianoRoll().setCursor({.track = track, .tick = 3 * o.q, .pitch = 60});
    const auto before = o.document.song();
    o.type(colon ? U"Qa:section-split Middle" : U"Qa,ss");
    o.press(NamedKey::Enter);
    REQUIRE(o.document.song().sections().size() == 2);
    CHECK(o.ws.message().find("cuts a bar short") != std::string::npos);
    CHECK(o.ws.messageKind() == MessageKind::Warning);
    CHECK_FALSE(o.ws.refused());
    o.type(U"4lQ");
    o.type(U"u@a");
    CHECK(o.document.song().sections().size() == 2);
    CHECK(o.ws.pianoRoll().cursor().tick == 4 * o.q);
    CHECK_FALSE(o.ws.refused());
    o.type(U"u");
    CHECK(o.document.song() == before);
}

TEST_CASE("A colon command's refusal stops a macro independently of message severity")
{
    const bool warning = GENERATE(false, true);
    const bool refused = GENERATE(false, true);
    Operating o;
    o.registry.add({.name = "outcome", .description = "Report a command outcome"},
                   [&](const CommandCall&)
                   { return CommandResult{true, "Reported", warning, refused}; });
    o.ws.setLineHandler([&](const std::string& line) { o.ws.report(o.registry.runLine(line)); });
    o.type(U"Qa:outcome");
    o.press(NamedKey::Enter);
    CHECK(o.ws.refused() == refused);
    CHECK(o.ws.messageKind() == (warning ? MessageKind::Warning : MessageKind::Information));
    o.type(U"jQk@a");
    CHECK(o.ws.arrangement().page().track() == (refused ? std::size_t{0} : std::size_t{1}));
    CHECK(o.ws.refused() == refused);
}
