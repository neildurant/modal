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

TEST_CASE("t and T move events later and earlier by the grid step")
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
    CHECK(o.ws.message() == "Undid: Move 2 notes in Bass earlier by a grid step");

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
    o.type(U"4l"); // bar 5, in the arrangement, with the sustain pedal down after the C2
    o.type(U"qq");
    CHECK(o.ws.message() == "Move the pedalling with its notes? (y/n)");
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
    o.type(U"gT>>");
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
        // The bottom line's text.
        const auto frame = o.ws.frame();
        const auto* bottom = &frame.front();
        for (const auto& region : frame)
        {
            if (region.bounds.y > bottom->bounds.y)
            {
                bottom = &region;
            }
        }
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

TEST_CASE("f and a note name go to the next note of that pitch class, and f and F then repeat it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    const auto bass = o.document.song().tracks()[0]->id();
    o.type(U"gr"); // the piano roll, at the start
    const auto at = [&] { return o.ws.pianoRoll().cursor(); };
    o.type(U"fa");
    CHECK(at() == SongCursor{bass, 4 * q, 45}); // A2, any octave
    CHECK(o.ws.pending() == "fA");
    o.type(U"f");
    CHECK(at() == SongCursor{bass, 28 * q, 33}); // A1
    o.type(U"f");
    CHECK(o.ws.message() == "No A after here in Bass");
    CHECK(at().tick == 28 * q);
    o.type(U"F");
    CHECK(at().tick == 4 * q);
    // Any other key ends it: f then takes a name again, in either case.
    o.type(U"jfC");
    CHECK(at() == SongCursor{bass, 16 * q + q / 16, 36});
    o.type(U"j");
    CHECK(o.ws.pending().empty());
    o.type(U"Fa");
    CHECK(at() == SongCursor{bass, 4 * q, 45});
    // A count goes as many on.
    o.type(U"gg2fa");
    CHECK(at().tick == 28 * q);
    o.type(U"2F");
    CHECK(at().tick == 28 * q); // only one before it
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
    o.press(NamedKey::Escape); // or f would find the next A
    o.type(U"fb");
    CHECK(o.ws.pianoRoll().cursor().tick == 12 * q); // B1
    o.type(U"b");
    CHECK(o.ws.pianoRoll().cursor().tick == 8 * q); // Bb1, from bar 2 again
    CHECK(o.ws.pianoRoll().cursor().pitch == 34);
    CHECK(o.ws.pending() == "fBb");
    o.type(U"f");
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
    // Another key, Escape among them, leaves it as it is, and is a key of its own, not part of
    // the change . repeats.
    o.type(U"dfa");
    o.press(NamedKey::Escape);
    CHECK(o.notes() == afterA);
    o.type(U"#");
    CHECK(o.notes() == afterA);
    o.type(U"u");
    o.type(U"}}dFaj");
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
    o.type(U"f");
    CHECK(o.ws.arrangement().cursor().tick == 28 * q); // bar 8
    o.type(U"jfa");
    CHECK(o.ws.message() == "No A after here in Lead");
    // In Visual mode, the selection follows, and f repeats.
    o.type(U"kggvfaf");
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
    CHECK(o.ws.handle(Key::typed(U'#'), 0)); // drawn, rather than waiting for the next key
    CHECK(o.ws.message() == "# isn't a key here");
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
